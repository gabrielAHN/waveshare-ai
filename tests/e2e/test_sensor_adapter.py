import importlib.util
import json
import os
import secrets
import ssl
import subprocess
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
ADAPTER = ROOT / "bridge" / "examples" / "home-assistant-sensor-adapter.py"
ENTITY_IDS = {
    "temperature": "sensor.room_temperature",
    "humidity": "sensor.room_humidity",
    "pressure": "sensor.room_pressure",
    "pm1": "sensor.room_pm1",
    "pm25": "sensor.room_pm25",
    "pm10": "sensor.room_pm10",
}
MAX_RESPONSE_BYTES = 65536


class HomeAssistantHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.requests.append((self.path, self.headers.get("Authorization")))
        redirect = self.server.redirects.get(self.path)
        if redirect is not None:
            status, location = redirect
            self.send_response(status)
            self.send_header("Location", location)
            self.end_headers()
            return
        body = self.server.responses.get(self.path, b'{"state":"unavailable"}')
        if not isinstance(body, bytes):
            body = json.dumps(body, separators=(",", ":")).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        if self.path not in self.server.streaming_paths:
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.server.response_bytes.append((self.path, len(body)))

    def log_message(self, format, *args):
        return


class SensorAdapterFunctionalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory(prefix="waveshare-sensor-adapter-")
        cls.temp_path = Path(cls.tempdir.name)
        cls.temp_path.chmod(0o700)
        cls.cert = cls.temp_path / "localhost.crt"
        cls.key = cls.temp_path / "localhost.key"
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256",
                "-days", "1", "-keyout", str(cls.key), "-out", str(cls.cert),
                "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        cls.cert.chmod(0o600)
        cls.key.chmod(0o600)

        cls.backend = ThreadingHTTPServer(("127.0.0.1", 0), HomeAssistantHandler)
        cls.backend.requests = []
        cls.backend.responses = {}
        cls.backend.redirects = {}
        cls.backend.streaming_paths = set()
        cls.backend.response_bytes = []
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cls.cert, cls.key)
        cls.backend.socket = tls.wrap_socket(cls.backend.socket, server_side=True)
        cls.backend_thread = threading.Thread(target=cls.backend.serve_forever, daemon=True)
        cls.backend_thread.start()

        cls.tls_catcher = ThreadingHTTPServer(("127.0.0.1", 0), HomeAssistantHandler)
        cls.tls_catcher.requests = []
        cls.tls_catcher.responses = {}
        cls.tls_catcher.redirects = {}
        cls.tls_catcher.streaming_paths = set()
        cls.tls_catcher.response_bytes = []
        catcher_tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        catcher_tls.load_cert_chain(cls.cert, cls.key)
        cls.tls_catcher.socket = catcher_tls.wrap_socket(cls.tls_catcher.socket, server_side=True)
        cls.tls_catcher_thread = threading.Thread(target=cls.tls_catcher.serve_forever, daemon=True)
        cls.tls_catcher_thread.start()

        cls.http_catcher = ThreadingHTTPServer(("127.0.0.1", 0), HomeAssistantHandler)
        cls.http_catcher.requests = []
        cls.http_catcher.responses = {}
        cls.http_catcher.redirects = {}
        cls.http_catcher.streaming_paths = set()
        cls.http_catcher.response_bytes = []
        cls.http_catcher_thread = threading.Thread(target=cls.http_catcher.serve_forever, daemon=True)
        cls.http_catcher_thread.start()

        cls.token = secrets.token_urlsafe(32)
        environment = {
            "HOME_ASSISTANT_URL": f"https://localhost:{cls.backend.server_port}",
            "HOME_ASSISTANT_TOKEN": cls.token,
            "SSL_CERT_FILE": str(cls.cert),
            **{f"SENSOR_{key.upper()}": entity for key, entity in ENTITY_IDS.items()},
        }
        cls.environment = mock.patch.dict(os.environ, environment)
        cls.environment.start()
        spec = importlib.util.spec_from_file_location("sensor_adapter_under_test", ADAPTER)
        cls.adapter_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.adapter_module)

        cls.adapter = ThreadingHTTPServer(("127.0.0.1", 0), cls.adapter_module.Handler)
        cls.adapter_thread = threading.Thread(target=cls.adapter.serve_forever, daemon=True)
        cls.adapter_thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.adapter.shutdown()
        cls.adapter.server_close()
        cls.adapter_thread.join(timeout=2)
        cls.http_catcher.shutdown()
        cls.http_catcher.server_close()
        cls.http_catcher_thread.join(timeout=2)
        cls.tls_catcher.shutdown()
        cls.tls_catcher.server_close()
        cls.tls_catcher_thread.join(timeout=2)
        cls.backend.shutdown()
        cls.backend.server_close()
        cls.backend_thread.join(timeout=2)
        cls.environment.stop()
        cls.tempdir.cleanup()

    def setUp(self):
        self.backend.requests.clear()
        self.backend.responses.clear()
        self.backend.redirects.clear()
        self.backend.streaming_paths.clear()
        self.backend.response_bytes.clear()
        self.tls_catcher.requests.clear()
        self.tls_catcher.responses.clear()
        self.tls_catcher.redirects.clear()
        self.tls_catcher.streaming_paths.clear()
        self.tls_catcher.response_bytes.clear()
        self.http_catcher.requests.clear()
        self.http_catcher.responses.clear()
        self.http_catcher.redirects.clear()
        self.http_catcher.streaming_paths.clear()
        self.http_catcher.response_bytes.clear()

    @staticmethod
    def entity_path(entity_id):
        return f"/api/states/{entity_id}"

    def request_adapter(self):
        url = f"http://127.0.0.1:{self.adapter.server_port}{self.adapter_module.PATH}"
        try:
            with urllib.request.urlopen(url, timeout=5) as response:
                body = response.read(MAX_RESPONSE_BYTES + 1)
                self.assertLessEqual(len(body), MAX_RESPONSE_BYTES)
                return response.status, body
        except urllib.error.HTTPError as error:
            try:
                body = error.read(MAX_RESPONSE_BYTES + 1)
                self.assertLessEqual(len(body), MAX_RESPONSE_BYTES)
                return error.code, body
            finally:
                error.close()

    def assert_backend_credentials(self, expected_reads):
        self.assertEqual(len(self.backend.requests), expected_reads)
        self.assertTrue(self.backend.requests)
        for _, authorization in self.backend.requests:
            self.assertEqual(authorization, f"Bearer {self.token}")

    def assert_explicit_upstream_error(self, backend_body):
        first_path = self.entity_path(ENTITY_IDS["temperature"])
        self.backend.responses[first_path] = backend_body
        status, body = self.request_adapter()
        self.assertEqual(status, 502)
        self.assertNotIn(self.token.encode(), body)
        self.assert_backend_credentials(1)

    @staticmethod
    def json_document_of_size(size):
        prefix = b'{"state":"ok","padding":"'
        suffix = b'"}'
        return prefix + (b"x" * (size - len(prefix) - len(suffix))) + suffix

    def test_upstream_read_is_bounded_to_limit_plus_one(self):
        class RecordingResponse:
            def __init__(self, body):
                self.body = body
                self.read_sizes = []

            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc_value, traceback):
                return False

            def read(self, size=-1):
                self.read_sizes.append(size)
                return self.body if size < 0 else self.body[:size]

        response = RecordingResponse(self.json_document_of_size(MAX_RESPONSE_BYTES))
        with mock.patch.object(self.adapter_module.OPENER, "open", return_value=response):
            value = self.adapter_module.state(ENTITY_IDS["temperature"])

        self.assertEqual(value, "ok")
        self.assertEqual(response.read_sizes, [MAX_RESPONSE_BYTES + 1])

    def test_oversized_upstream_is_rejected_for_content_length_and_streaming(self):
        first_path = self.entity_path(ENTITY_IDS["temperature"])
        oversized = self.json_document_of_size(MAX_RESPONSE_BYTES + 1)
        for mode in ("content-length", "streaming"):
            with self.subTest(mode=mode):
                self.backend.requests.clear()
                self.backend.responses.clear()
                self.backend.streaming_paths.clear()
                self.backend.response_bytes.clear()
                self.backend.responses[first_path] = oversized
                if mode == "streaming":
                    self.backend.streaming_paths.add(first_path)

                status, body = self.request_adapter()

                self.assertEqual(status, 502)
                self.assertNotIn(self.token.encode(), body)
                self.assertEqual(self.backend.response_bytes, [(first_path, MAX_RESPONSE_BYTES + 1)])
                self.assertEqual([path for path, _ in self.backend.requests], [first_path])
                self.assert_backend_credentials(1)

    def test_exactly_max_upstream_bytes_is_accepted_control(self):
        first_path = self.entity_path(ENTITY_IDS["temperature"])
        boundary = self.json_document_of_size(MAX_RESPONSE_BYTES)
        for mode in ("content-length", "streaming"):
            with self.subTest(mode=mode):
                self.backend.requests.clear()
                self.backend.responses.clear()
                self.backend.streaming_paths.clear()
                self.backend.response_bytes.clear()
                self.backend.responses[first_path] = boundary
                if mode == "streaming":
                    self.backend.streaming_paths.add(first_path)

                status, body = self.request_adapter()

                self.assertEqual(status, 200)
                self.assertEqual(json.loads(body)["sensors"]["temperature"], {"state": "ok"})
                self.assertNotIn(self.token.encode(), body)
                self.assert_backend_credentials(6)

    def test_state_utf8_values_over_256_bytes_are_rejected(self):
        first_path = self.entity_path(ENTITY_IDS["temperature"])
        cases = {
            "ascii": "a" * 257,
            "utf8": ("é" * 128) + "a",
            "json-escapes": ('"\\' * 128) + '"',
        }
        for name, value in cases.items():
            with self.subTest(name=name):
                self.backend.requests.clear()
                self.backend.responses.clear()
                self.backend.response_bytes.clear()
                self.backend.responses[first_path] = {"state": value}

                status, body = self.request_adapter()

                self.assertEqual(len(value.encode("utf-8")), 257)
                self.assertEqual(status, 502)
                self.assertNotIn(self.token.encode(), body)
                self.assert_backend_credentials(1)

    def test_state_utf8_values_at_256_bytes_are_accepted_control(self):
        first_path = self.entity_path(ENTITY_IDS["temperature"])
        cases = {
            "ascii": "a" * 256,
            "utf8": "é" * 128,
            "json-escapes": '"\\' * 128,
        }
        for name, value in cases.items():
            with self.subTest(name=name):
                self.backend.requests.clear()
                self.backend.responses.clear()
                self.backend.response_bytes.clear()
                self.backend.responses[first_path] = {"state": value}

                status, body = self.request_adapter()

                self.assertEqual(len(value.encode("utf-8")), 256)
                self.assertEqual(status, 200)
                self.assertLess(len(body), MAX_RESPONSE_BYTES)
                self.assertEqual(json.loads(body)["sensors"]["temperature"], {"state": value})
                self.assertNotIn(self.token.encode(), body)
                self.assert_backend_credentials(6)

    def test_six_maximum_state_values_fit_final_response_control(self):
        states = {
            "temperature": "a" * 256,
            "humidity": "é" * 128,
            "pressure": '"\\' * 128,
            "pm1": "b" * 256,
            "pm25": "ñ" * 128,
            "pm10": '\\"' * 128,
        }
        for key, value in states.items():
            self.assertEqual(len(value.encode("utf-8")), 256)
            self.backend.responses[self.entity_path(ENTITY_IDS[key])] = {"state": value}

        status, body = self.request_adapter()

        self.assertEqual(status, 200)
        self.assertLess(len(body), MAX_RESPONSE_BYTES)
        self.assertEqual(json.loads(body)["sensors"], {key: {"state": value} for key, value in states.items()})
        self.assertNotIn(self.token.encode(), body)
        self.assert_backend_credentials(6)

    def test_redirects_are_rejected_without_contacting_target(self):
        first_path = self.entity_path(ENTITY_IDS["temperature"])
        cases = [
            *[
                (f"https-{status}", status, f"https://localhost:{self.tls_catcher.server_port}/redirected", self.tls_catcher)
                for status in (301, 302, 303, 307, 308)
            ],
            ("http-downgrade", 302, f"http://localhost:{self.http_catcher.server_port}/redirected", self.http_catcher),
            ("same-origin", 302, f"https://localhost:{self.backend.server_port}/redirected", self.backend),
        ]
        for name, status_code, location, catcher in cases:
            with self.subTest(name=name):
                self.backend.requests.clear()
                self.backend.redirects.clear()
                self.tls_catcher.requests.clear()
                self.http_catcher.requests.clear()
                self.backend.redirects[first_path] = (status_code, location)

                status, body = self.request_adapter()

                self.assertEqual(status, 502)
                self.assertNotIn(self.token.encode(), body)
                if catcher is self.backend:
                    self.assertEqual([request for request in catcher.requests if request[0] == "/redirected"], [])
                else:
                    self.assertEqual(catcher.requests, [])
                self.assert_backend_credentials(1)

    def test_json_array_returns_explicit_502(self):
        self.assert_explicit_upstream_error([])

    def test_json_null_returns_explicit_502(self):
        self.assert_explicit_upstream_error(None)

    def test_valid_six_text_states_and_integer_now(self):
        states = {
            "temperature": "22.4",
            "humidity": "48.0",
            "pressure": "1013.2",
            "pm1": "2",
            "pm25": "4",
            "pm10": "unavailable",
        }
        for key, state in states.items():
            self.backend.responses[self.entity_path(ENTITY_IDS[key])] = {"state": state}

        status, body = self.request_adapter()

        self.assertEqual(status, 200)
        self.assertNotIn(self.token.encode(), body)
        payload = json.loads(body)
        self.assertIs(type(payload["now"]), int)
        self.assertEqual(payload["sensors"], {key: {"state": value} for key, value in states.items()})
        self.assertEqual(
            [path for path, _ in self.backend.requests],
            [self.entity_path(entity) for entity in ENTITY_IDS.values()],
        )
        self.assert_backend_credentials(6)

    def test_missing_and_nonstring_states_are_unavailable(self):
        first_path = self.entity_path(ENTITY_IDS["temperature"])
        for backend_body in ({}, {"state": 22}):
            with self.subTest(backend_body=backend_body):
                self.backend.requests.clear()
                self.backend.responses.clear()
                self.backend.responses[first_path] = backend_body
                status, body = self.request_adapter()
                self.assertEqual(status, 200)
                self.assertEqual(json.loads(body)["sensors"]["temperature"], {"state": "unavailable"})
                self.assertNotIn(self.token.encode(), body)
                self.assert_backend_credentials(6)

    def test_malformed_json_returns_explicit_502(self):
        self.assert_explicit_upstream_error(b"{")


if __name__ == "__main__":
    unittest.main()
