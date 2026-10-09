"""On-device enrollment: comparison code, multi-board registry, enrollment window, pinned TLS."""
import asyncio
import hashlib
import json
import os
import pathlib
import ssl
import stat
import struct
import subprocess
import tempfile
import time
import unittest

import aiohttp

from waveshare_bridge import enroll
from waveshare_bridge import live_bridge as bridge
from tests.test_transport import ROOT, serve

TOKEN_A = bytes(range(32, 64))
TOKEN_B = bytes([0x11] * 32)


def body(token, name='Waveshare AI'):
    raw = name.encode()[:32]
    return b'WEN1' + token + raw + b'\0' * (33 - len(raw))


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


class CodeTests(unittest.TestCase):
    def test_matches_firmware_vectors(self):
        # Same vectors as tests/host/pair_state_test.c (pair_code).
        self.assertEqual(enroll.comparison_code(bytes(range(32)), TOKEN_A), 546969)
        self.assertEqual(enroll.comparison_code(b'\xff' * 32, TOKEN_B), 616758)
        self.assertEqual(enroll.comparison_code(b'\xfe' + b'\xff' * 31, TOKEN_B), 162232)
        self.assertEqual(enroll.code_text(7), '000 007')
        self.assertEqual(enroll.code_text(546969), '546 969')

    def test_short_id_matches_firmware(self):
        self.assertEqual(enroll.short_id(TOKEN_A), hashlib.sha256(TOKEN_A).hexdigest()[:8])

    def test_parse_body(self):
        token, name = enroll.parse_enroll_body(body(TOKEN_A, 'Desk\x07 Board'))
        self.assertEqual(token, TOKEN_A)
        self.assertEqual(name, 'Desk Board')
        for bad in (b'', body(TOKEN_A)[:-1], b'WEN2' + body(TOKEN_A)[4:], body(b'\0' * 32), body(TOKEN_A) + b'x'):
            with self.subTest(n=len(bad)), self.assertRaises(ValueError):
                enroll.parse_enroll_body(bad)
        self.assertEqual(enroll.parse_enroll_body(body(TOKEN_A, ''))[1], 'Waveshare AI')


class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        self.path = pathlib.Path(self.tmp.name) / 'boards.json'

    def tearDown(self):
        self.tmp.cleanup()

    def test_multi_board_hash_only_private_file(self):
        reg = enroll.Registry(self.path)
        a = reg.add(TOKEN_A, 'Kitchen')
        b = reg.add(TOKEN_B, 'Desk')
        self.assertNotEqual(a['id'], b['id'])
        self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o600)
        text = self.path.read_text()
        self.assertNotIn(TOKEN_A.hex(), text)
        self.assertNotIn(TOKEN_B.hex(), text)
        self.assertIn(hashlib.sha256(TOKEN_A).hexdigest(), text)
        self.assertEqual(reg.match(TOKEN_A)['name'], 'Kitchen')
        self.assertEqual(reg.match(TOKEN_B)['name'], 'Desk')
        self.assertIsNone(reg.match(b'\x22' * 32))
        self.assertEqual([x['name'] for x in enroll.Registry(self.path).boards()], ['Kitchen', 'Desk'])
        # Re-enrolling the same identity replaces, never duplicates.
        reg.add(TOKEN_A, 'Kitchen 2')
        self.assertEqual(len(reg.boards()), 2)
        self.assertTrue(reg.remove(a['id']))
        self.assertIsNone(reg.match(TOKEN_A))
        self.assertFalse(reg.remove('deadbeef'))

    def test_reloads_external_changes_and_rejects_public_file(self):
        reg = enroll.Registry(self.path)
        reg.add(TOKEN_A, 'A')
        other = enroll.Registry(self.path)
        other.add(TOKEN_B, 'B')
        os.utime(self.path, (time.time() + 5, time.time() + 5))
        self.assertIsNotNone(reg.match(TOKEN_B))
        self.path.chmod(0o644)
        with self.assertRaises(ValueError):
            enroll.Registry(self.path).boards()

    def test_authorizer_bearer(self):
        reg = enroll.Registry(self.path)
        reg.add(TOKEN_A, 'A')
        auth = enroll.Authorizer(reg)
        self.assertTrue(auth.check(('Bearer ' + TOKEN_A.hex()).encode()))
        for bad in (b'', b'Bearer ', ('Bearer ' + 'c' * 64).encode(), ('Bearer ' + TOKEN_B.hex()).encode(), ('bearer ' + TOKEN_A.hex()).encode(),
                    ('Bearer ' + TOKEN_A.hex().upper()).encode(), ('Bearer ' + TOKEN_A.hex() + ' ').encode()):
            with self.subTest(bad=bad[:12]):
                self.assertFalse(auth.check(bad))


class WindowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        self.clock = Clock()
        self.reg = enroll.Registry(pathlib.Path(self.tmp.name) / 'boards.json')
        self.fp = b'\xab' * 32
        self.win = enroll.EnrollWindow(self.reg, self.fp, clock=self.clock)

    def tearDown(self):
        self.tmp.cleanup()

    def test_closed_window_refuses(self):
        self.assertEqual(self.win.submit(body(TOKEN_A), '10.0.0.5')[0], 403)
        self.assertEqual(self.reg.boards(), [])

    def test_confirm_flow(self):
        self.win.open(120)
        status, _ = self.win.submit(body(TOKEN_A), '10.0.0.5')
        self.assertEqual(status, 202)
        pending = self.win.pending()
        self.assertEqual(pending['code'], enroll.comparison_code(self.fp, TOKEN_A))
        self.assertEqual(pending['id'], enroll.short_id(TOKEN_A))
        self.assertEqual(self.win.submit(body(TOKEN_A), '10.0.0.5')[0], 202)  # board polls
        self.assertEqual(self.win.submit(body(TOKEN_B), '10.0.0.6')[0], 429)  # one at a time
        self.assertTrue(self.win.decide(True))
        self.assertEqual(self.win.submit(body(TOKEN_A), '10.0.0.5')[0], 200)
        self.assertEqual(self.reg.match(TOKEN_A)['name'], 'Waveshare AI')
        self.assertIsNone(self.win.pending())
        # Idempotent after acceptance (a lost 200 is retried by the board).
        self.assertEqual(self.win.submit(body(TOKEN_A), '10.0.0.5')[0], 200)

    def test_deny_and_expiry(self):
        self.win.open(300)
        self.win.submit(body(TOKEN_A), '10.0.0.5')
        self.assertTrue(self.win.decide(False))
        self.assertEqual(self.win.submit(body(TOKEN_A), '10.0.0.5')[0], 409)
        self.assertIsNone(self.reg.match(TOKEN_A))
        self.win.submit(body(TOKEN_B), '10.0.0.5')
        self.clock.now += enroll.PENDING_SECONDS + 1
        self.assertEqual(self.win.submit(body(TOKEN_B), '10.0.0.5')[0], 410)
        self.assertFalse(self.win.decide(True))
        self.assertIsNone(self.reg.match(TOKEN_B))

    def test_window_closes(self):
        self.win.open(10)
        self.clock.now += 11
        self.assertEqual(self.win.submit(body(TOKEN_A), '10.0.0.5')[0], 403)
        self.win.open(10)
        self.win.close()
        self.assertEqual(self.win.submit(body(TOKEN_A), '10.0.0.5')[0], 403)

    def test_rate_limit_and_bad_body(self):
        self.win.open(120)
        self.assertEqual(self.win.submit(b'junk', '10.0.0.5')[0], 400)
        codes = [self.win.submit(body(TOKEN_A), '10.0.0.9')[0] for _ in range(enroll.PER_IP_PER_MINUTE + 3)]
        self.assertEqual(codes[-1], 429)


def make_cert(root):
    cert, key = root / 'tls.crt', root / 'tls.key'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(key), '-out', str(cert),
                    '-days', '1', '-subj', '/CN=waveshare-bridge'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    return cert, key


async def peer_der(base):
    """Leaf certificate DER the server presents (what the board hashes and pins)."""
    host, port = base.split('//', 1)[1].rsplit(':', 1)
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    reader, writer = await asyncio.open_connection(host, int(port), ssl=ctx)
    der = writer.get_extra_info('ssl_object').getpeercert(binary_form=True)
    writer.close()
    return der


class PinnedClient:
    """Mimics the board: trusts the bridge iff its leaf certificate SHA-256 equals the pin."""
    def __init__(self, pin):
        self.pin = pin
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        self.ctx = ctx

    async def request(self, method, url, **kw):
        if hashlib.sha256(await peer_der(url.split('/v1/')[0])).digest() != self.pin:
            raise ssl.SSLError('pin mismatch')
        async with aiohttp.ClientSession() as s:
            async with s.request(method, url, ssl=self.ctx, **kw) as r:
                return r.status, await r.read(), None


class HTTPEnrollTests(unittest.IsolatedAsyncioTestCase):
    async def test_pinned_enroll_then_live(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = pathlib.Path(tmp)
            cert, key = make_cert(root)
            fp = enroll.cert_fingerprint(cert)
            self.assertEqual(fp, hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).digest())
            reg = enroll.Registry(root / 'boards.json')
            win = enroll.EnrollWindow(reg, fp)
            sample = bridge.Sample(b'k' * 32)
            sample.update([{'id': 'a', 'status': 'working'}])
            app = bridge.make_app(sample, enroll.Authorizer(reg), None, enroll=win)
            tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            tls.load_cert_chain(cert, key)
            runner, base = await serve(app, tls)
            try:
                c = PinnedClient(fp)
                self.assertEqual(hashlib.sha256(await peer_der(base)).digest(), fp)  # pin == served cert
                with self.assertRaises(ssl.SSLError):
                    await PinnedClient(b'\x01' * 32).request('POST', base + '/v1/enroll', data=body(TOKEN_A))
                st, _, _ = await c.request('POST', base + '/v1/enroll', data=body(TOKEN_A))
                self.assertEqual(st, 403)
                win.open(60)
                st, _, _ = await c.request('POST', base + '/v1/enroll', data=body(TOKEN_A))
                self.assertEqual(st, 202)
                st, _, _ = await c.request('GET', base + '/v1/enroll')
                self.assertEqual(st, 405)
                headers = {'Authorization': 'Bearer ' + TOKEN_A.hex()}
                st, _, _ = await c.request('GET', base + '/v1/live', headers=headers)
                self.assertEqual(st, 401)  # not yet confirmed
                win.decide(True)
                st, _, _ = await c.request('POST', base + '/v1/enroll', data=body(TOKEN_A))
                self.assertEqual(st, 200)
                st, data, _ = await c.request('GET', base + '/v1/live', headers=headers)
                self.assertEqual(st, 200)
                self.assertEqual(data[:4], b'WLS4')
                st, _, _ = await c.request('GET', base + '/v1/live', headers={'Authorization': 'Bearer ' + TOKEN_B.hex()})
                self.assertEqual(st, 401)
            finally:
                await runner.cleanup()


class ControlTests(unittest.IsolatedAsyncioTestCase):
    async def test_cli_confirms_over_control_socket(self):
        with tempfile.TemporaryDirectory() as tmp:  # AF_UNIX path length limit
            root = pathlib.Path(tmp)
            reg = enroll.Registry(root / 'boards.json')
            win = enroll.EnrollWindow(reg, b'\xab' * 32)
            server = await enroll.start_control(root / 'control.sock', win)
            try:
                self.assertEqual(stat.S_IMODE((root / 'control.sock').stat().st_mode), 0o600)
                answers = []

                def ask(p):
                    answers.append(p)
                    return 'y'

                async def board():
                    for _ in range(50):
                        if win.is_open():
                            break
                        await asyncio.sleep(0.02)
                    for _ in range(100):
                        status, _ = win.submit(body(TOKEN_A), '10.0.0.5')
                        if status != 202 and status != 403:
                            return status
                        await asyncio.sleep(0.05)
                task = asyncio.create_task(board())
                out = []
                result = await enroll.run_enroll_client(root / 'control.sock', 30, ask=ask, emit=out.append)
                self.assertEqual(result, 'accepted')
                self.assertEqual(await task, 200)
                code = enroll.code_text(enroll.comparison_code(b'\xab' * 32, TOKEN_A))
                self.assertTrue(any(code in line for line in out))
                self.assertFalse(any(TOKEN_A.hex() in line for line in out))
                self.assertIsNotNone(reg.match(TOKEN_A))
                self.assertFalse(win.is_open())  # the window closes with the CLI session
            finally:
                server.close()
                await server.wait_closed()

    async def test_cli_denies(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            reg = enroll.Registry(root / 'boards.json')
            win = enroll.EnrollWindow(reg, b'\xab' * 32)
            server = await enroll.start_control(root / 'control.sock', win)
            try:
                async def board():
                    while not win.is_open():
                        await asyncio.sleep(0.02)
                    for _ in range(100):
                        status, _ = win.submit(body(TOKEN_B), '10.0.0.5')
                        if status not in (202, 403):
                            return status
                        await asyncio.sleep(0.05)
                task = asyncio.create_task(board())
                result = await enroll.run_enroll_client(root / 'control.sock', 30, ask=lambda p: 'n', emit=lambda s: None)
                self.assertEqual(result, 'denied')
                self.assertEqual(await task, 409)
                self.assertIsNone(reg.match(TOKEN_B))
            finally:
                server.close()
                await server.wait_closed()


if __name__ == '__main__':
    unittest.main()
