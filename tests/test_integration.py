"""End-to-end checks of the actual TCP server and SQLite state."""
import os
import socket
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
        self.buffer = b""

    def ask(self, message, fragment=False):
        data = (message + "\r\n").encode("utf-8")
        if fragment:
            for byte in data:
                self.sock.sendall(bytes([byte]))
        else:
            self.sock.sendall(data)
        return self.recv()

    def recv(self):
        while b"\r\n" not in self.buffer:
            part = self.sock.recv(4096)
            assert part, "server disconnected"
            self.buffer += part
        line, self.buffer = self.buffer.split(b"\r\n", 1)
        text = line.decode("utf-8")
        assert "|" not in text.replace("\\n", ""), text
        return text

    def close(self):
        self.sock.close()


def start_server(port, db_path, seed=False):
    env = os.environ.copy()
    env["EXAM_ADMIN_CODE"] = "admin123"
    if not seed:
        env["EXAM_SKIP_SEED"] = "1"
    else:
        env.pop("EXAM_SKIP_SEED", None)
    proc = subprocess.Popen([str(SERVER), str(port), str(db_path)], cwd=ROOT, env=env,
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


def test_text_protocol():
    with tempfile.TemporaryDirectory() as folder:
        port = port_available()
        db_path = Path(folder) / "question.db"
        server = start_server(port, db_path, seed=True)
        peers = []
        try:
            owner, student, stranger = [Peer(port) for _ in range(3)]
            peers.extend((owner, student, stranger))
            assert student.ask("LIST ROOMS") == "401 LOGIN_REQUIRED"
            for peer, name in ((owner, "owner"), (student, "student"), (stranger, "other")):
                assert peer.ask(f'REGISTER {name} pw student ""', fragment=True) == "110"
                assert peer.ask(f"LOGIN {name} pw") == '111 "student"'
            assert student.ask("ADDQ x") == "400 UNKNOWN_COMMAND"
            assert student.ask("CHPASS pw next") == "400 UNKNOWN_COMMAND"
            assert owner.ask("CREATE ROOM room1 2 60") == "120"
            assert owner.ask("LIST ROOMS").startswith('121')
            assert student.ask("JOIN ROOM room1") == "122"
            assert stranger.ask("START ROOM room1") == "403 FORBIDDEN"
            student.sock.sendall(b"AWAIT ROOM room1\r\n")
            assert owner.ask("START ROOM room1") == "123"
            exam = student.recv()
            assert exam.startswith("125 ") and "Q" in exam
            assert stranger.ask("JOIN ROOM room1") == "409 ROOM_NOT_WAITING"
            assert student.ask("ANSWER ROOM room1 1 A") == "126"
            assert student.ask("ANSWER ROOM room1 1 B") == "126"
            assert student.ask("SUBMIT ROOM room1").startswith("127 ")
            assert student.ask("ANSWER ROOM room1 1 A") == "409 EXAM_CLOSED"
            assert owner.ask("RESULTS ROOM room1").startswith("129 ")
            practice = student.ask("CREATE PRACTICE 2 30")
            assert practice.startswith("131 ")
            import shlex
            practice_id = shlex.split(practice)[1]
            assert student.ask(f"QUESTIONS PRACTICE {practice_id} 1").startswith("132 ")
            assert student.ask(f"ANSWER PRACTICE {practice_id} 1 A") == "126"
            assert student.ask(f"SUBMIT PRACTICE {practice_id}").startswith("133 ")
            assert student.ask(f"STATUS PRACTICE {practice_id}").startswith("133 ")
            assert owner.ask("CREATE ROOM timed 1 10") == "120"
            assert student.ask("JOIN ROOM timed") == "122"
            assert owner.ask("START ROOM timed") == "123"
            time.sleep(11)
            assert student.recv().startswith("210 ")
            assert student.ask("STATUS ROOM timed").startswith("127 ")
            assert owner.ask('CREATE ROOM "phong co dau cach" 1 60') == "120"
            exam_ui = subprocess.Popen([str(CLIENT), "127.0.0.1", str(port)],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding="utf-8", cwd=ROOT)
            try:
                exam_ui.stdin.write('1\nexam_ui\npw\nstudent\n2\nexam_ui\npw\n3\nphong co dau cach\n')
                exam_ui.stdin.flush()
                for _ in range(100):
                    with closing(sqlite3.connect(db_path)) as db:
                        joined = db.execute("SELECT COUNT(*) FROM enrollments e JOIN rooms r ON r.id=e.room_id WHERE r.name=?", ("phong co dau cach",)).fetchone()[0]
                    if joined:
                        break
                    time.sleep(.05)
                assert joined == 1
                assert owner.ask('START ROOM "phong co dau cach"') == "123"
                stdout, stderr = exam_ui.communicate(input="1\nA\ns\ny\n0\n", timeout=10)
                assert exam_ui.returncode == 0 and "Your result:" in stdout, stdout + stderr
                assert " A. " in stdout and any(ord(c)>127 for c in stdout)
            finally:
                if exam_ui.poll() is None:
                    exam_ui.kill()
                    exam_ui.communicate()
            # Two commands in one TCP write remain two separate lines.
            owner.sock.sendall(b"LIST ROOMS\r\nLIST ROOMS\r\n")
            assert owner.recv().startswith("121")
            assert owner.recv().startswith("121")
            ui = subprocess.run([str(CLIENT), "127.0.0.1", str(port)],
                                input="2\nowner\npw\n6\n1\n30\nA\ny\n0\n",
                                text=True, encoding="utf-8", capture_output=True, timeout=10, cwd=ROOT)
            # Use an account without another active session.
            assert "ALREADY_LOGGED_IN" in ui.stdout
            assert owner.ask("LOGOUT") == "112"
            ui = subprocess.run([str(CLIENT), "127.0.0.1", str(port)],
                                input="2\nowner\npw\n6\n1\n30\nA\ny\n0\n",
                                text=True, encoding="utf-8", capture_output=True, timeout=10, cwd=ROOT)
            assert ui.returncode == 0 and "PSCORE" in ui.stdout
            assert any(ord(c) > 127 for c in ui.stdout)
            with closing(sqlite3.connect(db_path)) as db:
                assert db.execute("SELECT COUNT(*) FROM questions").fetchone()[0] == 20
                assert db.execute("PRAGMA foreign_key_check").fetchall() == []
        finally:
            for peer in peers:
                peer.close()
            stop_server(server)


if __name__ == "__main__":
    test_text_protocol()
    print("All text protocol integration checks passed.")
