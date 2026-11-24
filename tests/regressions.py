"""Headless regressions for malformed maps, save replacement and network stalls."""
import ctypes
import json
import pathlib
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

GAME, DRIVER, MAP = sys.argv[1:4]
sys.argv = sys.argv[:1]
_fixture_map=None
if not pathlib.Path(MAP).is_file():
    import source_fixtures
    _fixture_map=tempfile.TemporaryDirectory(prefix='zodee-regression-map-')
    MAP=str(pathlib.Path(_fixture_map.name)/'authored.map');source_fixtures.write_map(MAP)


class Header(ctypes.Structure):
    _fields_ = [('width', ctypes.c_ushort), ('height', ctypes.c_ushort),
                ('name', ctypes.c_char * 50), ('players', ctypes.c_ubyte),
                ('objects', ctypes.c_ushort), ('terrain', ctypes.c_ubyte),
                ('zones', ctypes.c_ushort)]

class MapObject(ctypes.Structure):
    _fields_ = [('x', ctypes.c_ushort), ('y', ctypes.c_ushort),
                ('owner', ctypes.c_byte), ('type', ctypes.c_ubyte),
                ('id', ctypes.c_ubyte), ('level', ctypes.c_byte),
                ('links', ctypes.c_ushort), ('health', ctypes.c_int)]

class Regressions(unittest.TestCase):
    def test_version_exits_without_startup_or_user_files(self):
        with tempfile.TemporaryDirectory() as folder:
            env={**os.environ,'HOME':folder,'USERPROFILE':folder,'ZOD_PROFILE_DIR':folder}
            for flag in ['--version','-v']:
                result=subprocess.run([str(pathlib.Path(GAME).resolve()),flag],cwd=folder,env=env,capture_output=True,text=True,timeout=3)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertIn('zod-enhanced',result.stdout)
            self.assertFalse(list(pathlib.Path(folder).iterdir()))

    def test_maps(self):
        original = pathlib.Path(MAP).read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            def check(data, valid):
                path = pathlib.Path(directory) / 'map.map'
                path.write_bytes(data)
                result = subprocess.run([GAME, '--map-info', str(path)], capture_output=True, timeout=5)
                self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)
                if valid:
                    metadata = json.loads(result.stdout)
                    self.assertEqual(metadata['players'], len(metadata['teams']))
                    self.assertEqual(sorted(metadata['teams']), ['blue', 'red'])
            check(original, True)
            invalid = bytearray(original)
            invalid[Header.terrain.offset] = 255
            check(invalid, False)
            for size in (0, 1, ctypes.sizeof(Header) - 1, len(original) - 1):
                check(original[:size], False)
            for field, value in [('width', 0), ('height', 0), ('width', 65535)]:
                invalid = bytearray(original)
                setattr(Header.from_buffer(invalid), field, value)
                check(invalid, False)
            # First tile follows the native-layout zone and object arrays.
            header = Header.from_buffer_copy(original)
            offset = ctypes.sizeof(Header) + header.zones * 8 + header.objects * ctypes.sizeof(MapObject)
            invalid = bytearray(original)
            invalid[offset:offset + 2] = (480).to_bytes(2, sys.byteorder)
            check(invalid, False)

    def test_save_failure_preserves_previous_save(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([DRIVER, 'save', str(pathlib.Path(directory) / 'save')], check=True, timeout=5)

    def test_save_serialization_roundtrip_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([DRIVER, 'save-serialization', str(pathlib.Path(directory) / 'save')], check=True, timeout=5)

    def test_menu_request_lifetime(self):
        subprocess.run([DRIVER, 'async'], check=True, timeout=5)

    def http(self, serve, path='/matches'):
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            listener.settimeout(5)
            errors = []
            def server():
                try:
                    with listener.accept()[0] as connection:
                        connection.recv(8192)
                        serve(connection)
                except (BrokenPipeError, ConnectionResetError):
                    pass  # deadline intentionally closes a slow response
                except Exception as error:
                    errors.append(error)
            worker = threading.Thread(target=server)
            worker.start()
            start = time.monotonic()
            result = subprocess.run([DRIVER, 'http', '127.0.0.1', str(listener.getsockname()[1]), path],
                                    capture_output=True, text=True, check=True, timeout=4)
            elapsed = time.monotonic() - start
            worker.join(5)
            self.assertFalse(worker.is_alive())
            self.assertFalse(errors, errors)
            return result.stdout, elapsed

    def test_local_server_startup(self):
        with tempfile.TemporaryDirectory(prefix='zod local hosting ') as directory:
            arguments = ['', 'two words', 'ending\\', 'embedded"quote', 'backslash\\"quote', '&literal']
            for mode in ('exit', 'hang', 'listen'):
                script = pathlib.Path(directory) / 'server with spaces.py'
                script.write_text("import sys\nassert sys.argv[1:] == " + repr(arguments) + "\n" + {
                    'exit': 'raise SystemExit(17)',
                    'hang': 'import time; time.sleep(30)',
                    'listen': "import socket, os, time; s=socket.socket(); s.bind(('127.0.0.1', int(os.environ['ZOD_PORT']))); s.listen(); time.sleep(30)",
                }[mode])
                with socket.socket() as reserve:
                    reserve.bind(('127.0.0.1', 0))
                    port = reserve.getsockname()[1]
                result = subprocess.run([DRIVER, 'local', sys.executable, str(script), str(port), *arguments],
                                        env={**os.environ, 'ZOD_PORT': '32199', 'ZOD_EMPTY_EXIT_SECS': '42'},
                                        capture_output=True, text=True, check=True, timeout=8)
                self.assertTrue(result.stdout.startswith('1 ' if mode == 'listen' else '0 '), result.stdout)
                if mode != 'listen':
                    self.assertIn('exited' if mode == 'exit' else 'timed out', result.stdout)
                # A failed startup must not leave its child listening/running.
                with socket.socket() as probe:
                    self.assertNotEqual(probe.connect_ex(('127.0.0.1', port)), 0)

    def test_local_server_spawn_error(self):
        with tempfile.TemporaryDirectory(prefix='zod missing server ') as directory:
            result = subprocess.run([DRIVER, 'local', str(pathlib.Path(directory) / 'missing.exe'),
                                     'unused argument', '23456'], capture_output=True, text=True, check=True, timeout=5)
            self.assertTrue(result.stdout.startswith('0 Cannot start server'), result.stdout)

    def test_http_success_and_long_request(self):
        response, _ = self.http(lambda c: c.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[]'), '/' + 'x' * 1000)
        self.assertEqual(response, '1 200\n[]')

    def test_http_truncated_response(self):
        response, _ = self.http(lambda c: c.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n[]'))
        self.assertEqual(response, '0 0\n')

    def test_http_overall_deadline(self):
        def drip(connection):
            connection.sendall(b'HTTP/1.1 200 OK\r\n\r\n')
            for _ in range(12):
                connection.sendall(b'x')
                time.sleep(0.25)
        response, elapsed = self.http(drip)
        self.assertEqual(response, '0 0\n')
        self.assertLess(elapsed, 3)

unittest.main()
