"""End-to-end checks of the actual TCP server and SQLite state."""
import os
import socket
import subprocess
import tempfile
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
                assert peer.ask(f'REGISTER {name} pw', fragment=True) == "110"
                assert peer.ask(f"LOGIN {name} pw") == "111"
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
            with closing(sqlite3.connect(db_path)) as db:
                correct = db.execute("SELECT q.correct FROM practice_questions pq JOIN questions q ON q.id=pq.question_id WHERE pq.practice_id=? AND pq.position=1", (practice_id,)).fetchone()[0]
            assert student.ask(f"ANSWER PRACTICE {practice_id} 1 {correct}") == "126"
            assert student.ask(f"SUBMIT PRACTICE {practice_id}") == '133 "1" "2"'
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
                exam_ui.stdin.write('1\nexam_ui\npw\n2\nexam_ui\npw\n3\nphong co dau cach\n')
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
            assert ui.returncode == 0 and "SCORE|PRACTICE" in ui.stdout
            assert any(ord(c) > 127 for c in ui.stdout)
            with closing(sqlite3.connect(db_path)) as db:
                assert db.execute("SELECT COUNT(*) FROM questions").fetchone()[0] == 20
                assert db.execute("PRAGMA foreign_key_check").fetchall() == []
        finally:
            for peer in peers:
                peer.close()
            stop_server(server)



def login_peer(port, name):
    peer = Peer(port)
    assert peer.ask(f"REGISTER {name} pw") == "110"
    assert peer.ask(f"LOGIN {name} pw") == "111"
    return peer


def wait_for_login(peer, name):
    for _ in range(50):
        result = peer.ask(f"LOGIN {name} pw")
        if result == "111":
            return
        assert result == "409 ALREADY_LOGGED_IN", result
        time.sleep(.05)
    raise AssertionError("disconnected session was not released")


def test_sessions_results_restart():
    import shlex
    with tempfile.TemporaryDirectory() as folder:
        db_path = Path(folder) / "question.db"
        port = port_available()
        server = start_server(port, db_path, seed=True)
        peers = []
        try:
            owner, alice, bob = [login_peer(port, n) for n in ("teacher", "alice", "bob")]
            peers.extend((owner, alice, bob))
            duplicate = Peer(port)
            peers.append(duplicate)
            assert duplicate.ask("LOGIN alice pw") == "409 ALREADY_LOGGED_IN"
            assert duplicate.ask("LIST ROOMS") == "401 LOGIN_REQUIRED"
            with closing(sqlite3.connect(db_path)) as db:
                hashes = db.execute("SELECT password FROM users").fetchall()
                assert all(value.startswith("pbkdf2$") and value != "pw" for value, in hashes)
                # Legacy accounts remain usable without an admin role in the protocol.
                db.execute("UPDATE users SET role='admin' WHERE username='teacher'")
                db.commit()
            assert owner.ask("LOGOUT") == "112"
            assert owner.ask("LOGIN teacher pw") == "111"
            assert owner.ask("CREATE ROOM lobby 2 60") == "120"
            assert alice.ask("JOIN ROOM lobby") == "122"
            alice.sock.sendall(b"AWAIT ROOM lobby\r\n")
            time.sleep(.15)
            alice.close()
            wait_for_login(duplicate, "alice")
            assert duplicate.ask("JOIN ROOM lobby") == "122"
            assert bob.ask("JOIN ROOM lobby") == "122"
            assert owner.ask("START ROOM lobby") == "123"
            exam = shlex.split(duplicate.ask("QUESTIONS ROOM lobby"))
            assert exam[:2] == ["125", "lobby"] and exam[3] == "2"
            with closing(sqlite3.connect(db_path)) as db:
                rows = db.execute("SELECT rq.position,q.text,q.a,q.b,q.c,q.d,q.correct FROM room_questions rq JOIN questions q ON q.id=rq.question_id JOIN rooms r ON r.id=rq.room_id WHERE r.name='lobby' ORDER BY rq.position").fetchall()
            # Exact payload comparison: no extra correct-answer field is transmitted.
            expected = exam[:4]
            for pos, text, a, b, c, d, correct in rows:
                expected.extend(("ROW", "Q", str(pos), text, a, b, c, d, ""))
                assert duplicate.ask(f"ANSWER ROOM lobby {pos} {correct}") == "126"
            assert exam == expected, exam
            duplicate.close()
            resumed = Peer(port)
            peers.append(resumed)
            wait_for_login(resumed, "alice")
            restored = shlex.split(resumed.ask("QUESTIONS ROOM lobby"))
            expected = restored[:4]
            for pos, text, a, b, c, d, correct in rows:
                expected.extend(("ROW", "Q", str(pos), text, a, b, c, d, correct))
            assert restored == expected
            assert resumed.ask("SUBMIT ROOM lobby") == '127 "2" "2"'
            assert owner.ask("RESULTS ROOM lobby") == "409 ROOM_NOT_FINISHED"
            assert bob.ask("SUBMIT ROOM lobby") == '127 "0" "2"'
            expected_results = ['129', 'ROW', 'R', 'alice', '2', '2', '0', 'ROW', 'R', 'bob', '0', '2', '0']
            assert shlex.split(owner.ask("RESULTS ROOM lobby")) == expected_results
        finally:
            for peer in peers:
                peer.close()
            stop_server(server)
        server = start_server(port, db_path, seed=True)
        try:
            reader = Peer(port)
            try:
                assert reader.ask("LOGIN teacher pw") == "111"
                assert shlex.split(reader.ask("RESULTS ROOM lobby")) == expected_results
                assert '"FINISHED"' in reader.ask("LIST ROOMS")
                with closing(sqlite3.connect(db_path)) as db:
                    assert db.execute("SELECT COUNT(*) FROM questions").fetchone()[0] == 20
                    assert db.execute("SELECT COUNT(*) FROM exam_answers").fetchone()[0] == 2
                    assert db.execute("PRAGMA foreign_key_check").fetchall() == []
            finally:
                reader.close()
        finally:
            stop_server(server)


def test_quoting_escape_and_validation():
    import shlex
    with tempfile.TemporaryDirectory() as folder:
        port = port_available()
        db_path = Path(folder) / "question.db"
        server = start_server(port, db_path, seed=True)
        peer = None
        try:
            peer = login_peer(port, "codec")
            # Includes Vietnamese, spaces, a quote and a backslash. Verify round trip.
            name = 'Phòng "A" \\ test'
            quoted = '"' + name.replace('\\', '\\\\').replace('"', '\\"') + '"'
            assert peer.ask(f"CREATE ROOM {quoted} 1 30", fragment=True) == "120"
            fields = shlex.split(peer.ask("LIST ROOMS"))
            assert fields == ['121', 'ROW', 'R', name, 'WAITING', '1', '30', 'codec']
            assert peer.ask('CREATE ROOM "unterminated 1 30') == "400 BAD_REQUEST"
            assert peer.ask(r'CREATE ROOM "bad\z" 1 30') == "400 BAD_REQUEST"
            assert peer.ask('REGISTER unwanted pw student ""') == "400 BAD_REQUEST"
            assert peer.ask("CREATE ROOM wrong 0 30") == "400 BAD_REQUEST"
            for obsolete in ("PQUESTION 1 1", "PANSWER 1 1 A", "PSUBMIT 1", "PSTATUS 1", "PRACTICE 1 30"):
                assert peer.ask(obsolete) == "400 UNKNOWN_COMMAND"
            assert peer.ask("LOGOUT") == "112"
            assert peer.ask("REGISTER ROOM pw") == "110"
            assert peer.ask("LOGIN ROOM pw") == "111"
            assert peer.ask("LOGOUT") == "112"
            assert peer.ask("LOGIN codec pw") == "111"
            assert peer.ask("CREATE PRACTICE 21 30") == "409 NOT_ENOUGH_QUESTIONS"
            assert peer.ask("START ROOM missing") == "404 ROOM_NOT_FOUND"
            assert peer.ask(f"START ROOM {quoted}") == "409 NO_PARTICIPANTS"
            assert peer.ask("LOGOUT") == "112"
            assert peer.ask("LOGIN codec wrong") == "401 CREDENTIALS"
            assert peer.ask("LOGIN codec pw") == "111"
            practice = shlex.split(peer.ask("CREATE PRACTICE 1 10"))
            time.sleep(11)
            assert peer.ask(f"STATUS PRACTICE {practice[1]}") == '133 "0" "1"'
        finally:
            if peer:
                peer.close()
            stop_server(server)


if __name__ == "__main__":
    for test in (test_text_protocol, test_sessions_results_restart, test_quoting_escape_and_validation):
        test()
        print(f"PASS: {test.__name__}")
    print("All text protocol integration checks passed.")
