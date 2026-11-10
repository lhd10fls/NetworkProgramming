"""End-to-end checks of the actual TCP server and SQLite state."""
import os
import socket
import struct
import subprocess
import tempfile
import threading
import time
import sqlite3
from contextlib import closing
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / ("server.exe" if os.name == "nt" else "server")
CLIENT = ROOT / ("client.exe" if os.name == "nt" else "client")


class Peer:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), 2)
        self.sock.settimeout(35)

    def ask(self, message, fragment=False):
        data = message.encode()
        frame = struct.pack("!I", len(data)) + data
        if fragment:
            for byte in frame:
                self.sock.sendall(bytes([byte]))
        else:
            self.sock.sendall(frame)
        return self.recv()

    def recv(self):
        def exact(count):
            data = b""
            while len(data) < count:
                part = self.sock.recv(count - len(data))
                assert part, "server disconnected"
                data += part
            return data
        length = struct.unpack("!I", exact(4))[0]
        assert length < 65536
        return exact(length).decode()

    def close(self):
        self.sock.close()


def start_server(port, db_path):
    proc = subprocess.Popen([str(SERVER), str(port), str(db_path)], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    for _ in range(100):
        if proc.poll() is not None:
            out, err = proc.communicate()
            raise AssertionError(f"server exited: {out!r} {err!r}")
        try:
            p = Peer(port)
            p.close()
            return proc
        except OSError:
            time.sleep(.05)
    proc.kill()
    raise AssertionError("server did not listen")


def stop_server(proc):
    proc.terminate()
    try:
        proc.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate(timeout=3)


def port_available():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_full_flow():
    with tempfile.TemporaryDirectory() as folder:
        db_path = Path(folder) / "exam.db"
        port = port_available()
        server = start_server(port, db_path)
        try:
            anonymous = Peer(port)
            assert anonymous.ask("LIST") == "ERR|LOGIN_REQUIRED"
            anonymous.close()

            admin, student, stranger = Peer(port), Peer(port), Peer(port)
            assert admin.ask("REGISTER|teacher|pw|admin|admin123", fragment=True) == "OK|REGISTERED"
            assert student.ask("REGISTER|alice|pw|student|") == "OK|REGISTERED"
            assert stranger.ask("REGISTER|bob|pw|student|") == "OK|REGISTERED"
            assert admin.ask("LOGIN|teacher|pw") == "OK|LOGIN|admin"
            assert student.ask("LOGIN|alice|pw") == "OK|LOGIN|student"
            assert stranger.ask("LOGIN|bob|pw") == "OK|LOGIN|student"
            with closing(sqlite3.connect(db_path)) as check_db:
                assert check_db.execute("SELECT password FROM users WHERE username='alice'").fetchone()[0].startswith("pbkdf2$")
            # Two frames in one TCP write must still produce two responses.
            first, second = b"LIST", b"LIST"
            frames = struct.pack("!I", len(first)) + first + struct.pack("!I", len(second)) + second
            admin.sock.sendall(frames)
            assert admin.recv().startswith("ROOMS") and admin.recv().startswith("ROOMS")
            assert student.ask("CREATE|wrong|1|10") == "ERR|NOT_ENOUGH_QUESTIONS"
            assert admin.ask("CREATE|empty|1|10") == "ERR|NOT_ENOUGH_QUESTIONS"
            questions = [
                "ADDQ|2+2=?|3|4|5|6|B|math|easy",
                "ADDQ|Capital of France?|Paris|Rome|Berlin|London|A|geography|easy",
                "ADDQ|TCP is?|Protocol|Database|OS|Language|A|network|medium",
            ]
            for q in questions:
                assert admin.ask(q).startswith("OK|QUESTION|")
            ui = subprocess.run([str(CLIENT), "127.0.0.1", str(port)],
                                input="2\nalice\npw\n6\n1\n10\nA\ny\n0\n",
                                text=True, capture_output=True, timeout=10, cwd=ROOT)
            assert ui.returncode == 0 and "PSCORE|" in ui.stdout, ui.stdout + ui.stderr
            assert student.ask("ADDQ|x|a|b|c|d|A|t|easy") == "ERR|FORBIDDEN"
            assert admin.ask("CREATE|midterm|2|30") == "OK|ROOM|WAITING"
            assert "R|midterm|WAITING|2|30|teacher" in student.ask("LIST")
            assert student.ask("JOIN|midterm") == "OK|JOINED"
            assert student.ask("RESULTS|midterm") == "ERR|ROOM_NOT_FINISHED"
            assert stranger.ask("EXAM|midterm") == "ERR|NOT_PARTICIPANT"
            result = []
            waiter = threading.Thread(target=lambda: result.append(student.ask("WAIT|midterm")))
            waiter.start()
            time.sleep(.2)
            assert admin.ask("START|midterm") == "OK|STARTED"
            waiter.join(3)
            assert result and result[0].startswith("EXAM|midterm|")
            assert result[0].count("\nQ|") == 2
            assert stranger.ask("JOIN|midterm") == "ERR|ROOM_NOT_WAITING"
            assert "R|midterm|RUNNING|2|30|teacher" in admin.ask("LIST")
            for row in result[0].splitlines()[1:]:
                parts = row.split("|")
                pos = int(parts[1])
                correct = {"2+2=?": "B", "Capital of France?": "A", "TCP is?": "A"}[parts[2]]
                assert student.ask(f"ANSWER|midterm|{pos}|D") == "OK|ANSWER_SAVED"
                assert student.ask(f"ANSWER|midterm|{pos}|{correct}") == "OK|ANSWER_SAVED"
            rejoined = Peer(port)
            assert rejoined.ask("LOGIN|alice|pw") == "OK|LOGIN|student"
            restored = rejoined.ask("EXAM|midterm")
            for row in restored.splitlines()[1:]:
                parts = row.split("|")
                assert parts[-1] == {"2+2=?": "B", "Capital of France?": "A", "TCP is?": "A"}[parts[2]]
            rejoined.close()
            assert student.ask("SUBMIT|midterm") == "SCORE|2|2"
            assert "R|midterm|FINISHED|2|30|teacher" in admin.ask("LIST")
            assert "R|alice|2|2|0" in admin.ask("RESULTS|midterm")
            assert student.ask("SUBMIT|midterm") == "SCORE|2|2"

            practice = student.ask("PRACTICE|2|10")
            assert practice.startswith("PRACTICE|")
            pid = int(practice.split("|")[1])
            q = student.ask(f"PQUESTION|{pid}|1")
            assert q.startswith("Q|1|") and "ANSWER" not in q
            correct = {"2+2=?": "B", "Capital of France?": "A", "TCP is?": "A"}[q.split("|")[2]]
            assert student.ask(f"PANSWER|{pid}|1|{correct}") == "OK|ANSWER_SAVED"
            assert student.ask(f"PSUBMIT|{pid}") == "PSCORE|1|2"

            assert student.ask("CREATE|student_room|1|10") == "OK|ROOM|WAITING"
            assert stranger.ask("JOIN|student_room") == "OK|JOINED"
            assert admin.ask("START|student_room") == "ERR|FORBIDDEN"
            assert student.ask("START|student_room") == "OK|STARTED"
            assert stranger.ask("EXAM|student_room").startswith("EXAM|student_room|")
            assert stranger.ask("SUBMIT|student_room") == "SCORE|0|1"

            assert admin.ask("CREATE|group|1|30") == "OK|ROOM|WAITING"
            assert student.ask("JOIN|group") == "OK|JOINED"
            assert stranger.ask("JOIN|group") == "OK|JOINED"
            assert admin.ask("START|group") == "OK|STARTED"
            assert student.ask("SUBMIT|group") == "SCORE|0|1"
            assert admin.ask("RESULTS|group") == "ERR|ROOM_NOT_FINISHED"
            assert stranger.ask("SUBMIT|group") == "SCORE|0|1"
            assert admin.ask("RESULTS|group").count("\nR|") == 2

            assert admin.ask("CREATE|timed|1|10") == "OK|ROOM|WAITING"
            assert student.ask("JOIN|timed") == "OK|JOINED"
            ui_waiter = subprocess.Popen([str(CLIENT), "127.0.0.1", str(port)],
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, text=True, cwd=ROOT)
            ui_waiter.stdin.write("2\nbob\npw\n3\ntimed\n")
            ui_waiter.stdin.flush()
            time.sleep(.5)
            assert admin.ask("START|timed") == "OK|STARTED"
            assert student.ask("EXAM|timed").startswith("EXAM|timed|")
            auto_practice = student.ask("PRACTICE|1|10")
            assert auto_practice.startswith("PRACTICE|")
            auto_pid = int(auto_practice.split("|")[1])
            time.sleep(11)
            assert student.recv() == "EVENT|AUTO_SCORE|timed|0|1"
            ui_out, ui_err = ui_waiter.communicate(input="0\n", timeout=5)
            assert ui_waiter.returncode == 0 and "[Server] Time is up in timed." in ui_out, ui_out + ui_err
            assert student.ask("STATUS|timed") == "SCORE|0|1"
            assert student.ask(f"PSTATUS|{auto_pid}") == "PSCORE|0|1"
            assert "R|alice|0|1|1" in admin.ask("RESULTS|timed")
            assert student.ask("ANSWER|timed|1|A") == "ERR|EXAM_CLOSED"
            assert student.ask("CHPASS|pw|newpw") == "OK|PASSWORD_CHANGED"
            assert student.ask("LOGOUT") == "OK|LOGOUT"
            assert student.ask("LOGIN|alice|pw") == "ERR|CREDENTIALS"
            assert student.ask("LOGIN|alice|newpw") == "OK|LOGIN|student"
            admin.close(); student.close(); stranger.close()
        finally:
            stop_server(server)
        with closing(sqlite3.connect(db_path)) as check_db:
            assert check_db.execute("SELECT COUNT(*) FROM logs WHERE event='START_ROOM'").fetchone()[0] >= 3

        server = start_server(port, db_path)
        try:
            admin = Peer(port)
            assert admin.ask("LOGIN|teacher|pw") == "OK|LOGIN|admin"
            rooms = admin.ask("LIST")
            assert "R|midterm|FINISHED" in rooms and "R|timed|FINISHED" in rooms
            assert "R|alice|2|2|0" in admin.ask("RESULTS|midterm")
            admin.close()
        finally:
            stop_server(server)


if __name__ == "__main__":
    test_full_flow()
    print("All integration checks passed.")
