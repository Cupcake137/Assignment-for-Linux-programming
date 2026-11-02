"""Black-box tests using isolated working directories and localhost sockets."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import signal
import socket
import sqlite3
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def wait_until(check, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.03)
    raise AssertionError('Timed out waiting for expected state')


class Server:
    def __init__(self, directory, map_path=None, db_path=None, ready=True):
        self.directory = Path(directory)
        self.log = self.directory / 'gateway.log'
        self.db = Path(db_path) if db_path else self.directory / 'sensor.db'
        self.log.unlink(missing_ok=True)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            self.port = sock.getsockname()[1]
        self.process = subprocess.Popen(
            [str(ROOT/'gateway'), str(self.port),
             str(map_path or ROOT/'config/room_sensor.map'), str(self.db)],
            cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, start_new_session=True)
        if ready:
            try:
                wait_until(lambda: 'listening on' in self.text())
            except Exception:
                self.abort()
                raise

    def text(self):
        return self.log.read_text() if self.log.is_file() else ''

    def count(self):
        if not self.db.exists():
            return -1
        try:
            with sqlite3.connect(self.db, timeout=.2) as db:
                return db.execute('SELECT COUNT(*) FROM sensor_data').fetchone()[0]
        except sqlite3.Error:
            return -1

    def abort(self):
        # Kill the isolated process group so test failures do not leave children.
        try:
            os.killpg(self.process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.process.communicate()

    def finish(self, expected=0):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
        try:
            out, err = self.process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            self.abort()
            raise AssertionError('Gateway did not shut down')
        assert self.process.returncode == expected, (out, err)
        assert 'AddressSanitizer' not in err and 'runtime error:' not in err, err
        assert not (self.directory/'logFifo').exists()


def send_raw(port, data, fragmented=False):
    with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
        if fragmented:
            for i in range(0, len(data), 3):
                sock.sendall(data[i:i+3])
                time.sleep(.001)
        else:
            sock.sendall(data)


def send(port, sensor, values, fragmented=False):
    data = ''.join(f'{sensor} {value} {1000+i}\n' for i, value in enumerate(values))
    send_raw(port, data.encode(), fragmented)


def concurrent_clients():
    with tempfile.TemporaryDirectory() as directory:
        server = Server(directory)
        try:
            with ThreadPoolExecutor(max_workers=5) as pool:
                jobs = [pool.submit(send, server.port, sensor, [21.0]*100)
                        for sensor in (10, 20, 30, 40, 50)]
                for job in jobs:
                    job.result()
            send(server.port, 999, [22.0])
            send(server.port, 20, [22.0, 23.0], fragmented=True)
            subprocess.run([str(ROOT/'sensor_node'), '127.0.0.1', str(server.port),
                            '50', '24', '5', '0'], check=True, timeout=5)
            send_raw(server.port, b'20 nan 1\n')
            send_raw(server.port, b'20 21 1\n50 21 2\n')
            send_raw(server.port, b'20 2')
            send_raw(server.port, b'x'*300)
            wait_until(lambda: server.count() == 509)
            wait_until(lambda: 'incomplete line discarded' in server.text())
            wait_until(lambda: 'oversized line' in server.text())
            # An idle, partial-line connection must not block shutdown.
            idle = socket.create_connection(('127.0.0.1', server.port))
            idle.sendall(b'20 ')
            try:
                server.finish()
            finally:
                idle.close()
            text = server.text()
            for expected in ('invalid sensor node ID 999', 'invalid measurement',
                             'sensor ID changed', 'Gateway shutting down'):
                assert expected in text, expected
            lines = text.splitlines()
            assert [int(line.split()[0]) for line in lines] == list(range(1, len(lines)+1))
        except Exception:
            server.abort()
            raise
        server = Server(directory)
        try:
            send(server.port, 20, [25.0])
            wait_until(lambda: server.count() == 510)
        finally:
            server.finish()
        assert 'New table' not in server.text()
    print('PASS clients: concurrent senders, framing, validation, restart, idle shutdown', flush=True)


def averages():
    with tempfile.TemporaryDirectory() as directory:
        server = Server(directory)
        try:
            send(server.port, 20, [30.0]*4)
            wait_until(lambda: server.count() == 4)
            assert 'too hot' not in server.text()
            send(server.port, 20, [30.0] + [10.0]*5)
            wait_until(lambda: server.count() == 10)
        finally:
            server.finish()
        text = server.text()
        assert text.count('too hot') == 1
        assert text.count('too cold') == 2
        assert 'temperature = 30.00' in text and 'temperature = 14.00' in text
        assert 'room = 602' in text
    print('PASS analysis: warm-up, rolling replacement, boundaries, room mapping', flush=True)


def delayed_database():
    with tempfile.TemporaryDirectory() as directory:
        parent = Path(directory)/'delayed'
        server = Server(directory, db_path=parent/'sensor.db')
        try:
            wait_until(lambda: 'attempt 1/3' in server.text())
            send(server.port, 20, [21.0]*10)
            parent.mkdir()
            wait_until(lambda: server.count() == 10, timeout=9)
        finally:
            server.finish()
    print('PASS database: unavailable at startup, then recovery without lost pending data', flush=True)


def permanent_database_failure():
    with tempfile.TemporaryDirectory() as directory:
        server = Server(directory, db_path=Path(directory)/'missing/sensor.db')
        try:
            wait_until(lambda: server.process.poll() is not None, timeout=14)
        except Exception:
            server.abort()
            raise
        server.finish(expected=1)
        assert all(f'attempt {n}/3' in server.text() for n in (1, 2, 3))
    print('PASS database: three initialization failures stop the gateway', flush=True)


def insert_failure(recover):
    with tempfile.TemporaryDirectory() as directory:
        server = Server(directory)
        try:
            wait_until(lambda: server.count() == 0)
            with sqlite3.connect(server.db) as db:
                db.execute("CREATE TRIGGER fail_insert BEFORE INSERT ON sensor_data "
                           "BEGIN SELECT RAISE(ABORT, 'injected write failure'); END")
            send(server.port, 20, [21.0]*5)
            wait_until(lambda: 'SQLite write failed' in server.text())
            assert server.count() == 0
            if recover:
                with sqlite3.connect(server.db) as db:
                    db.execute('DROP TRIGGER fail_insert')
                wait_until(lambda: server.count() == 5, timeout=9)
            else:
                wait_until(lambda: server.process.poll() is not None, timeout=14)
                assert server.count() == 0
        except Exception:
            server.abort()
            raise
        server.finish(expected=0 if recover else 1)
        if not recover:
            assert server.text().count('SQLite write failed') == 3
    print('PASS INSERT: ' + ('pending reading recovers' if recover else 'persistent failure stops'), flush=True)


def bad_map():
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)/'bad.map'
        path.write_text('501 20\n602 20\n')
        server = Server(directory, map_path=path, ready=False)
        try:
            wait_until(lambda: server.process.poll() is not None)
        except Exception:
            server.abort()
            raise
        server.finish(expected=1)
        assert 'duplicate IDs' in server.text()
    print('PASS configuration: duplicate map entries rejected', flush=True)


def idle_timeout():
    with tempfile.TemporaryDirectory() as directory:
        server = Server(directory)
        try:
            with socket.create_connection(('127.0.0.1', server.port), timeout=13) as sock:
                sock.sendall(b'20 21 1\n')
                wait_until(lambda: server.count() == 1)
                assert sock.recv(1) == b''
            assert 'idle timeout' in server.text()
        finally:
            server.finish()
    print('PASS clients: idle connections are closed', flush=True)


if __name__ == '__main__':
    concurrent_clients()
    averages()
    delayed_database()
    permanent_database_failure()
    insert_failure(recover=True)
    insert_failure(recover=False)
    bad_map()
    idle_timeout()
