import asyncio
import pathlib
import ssl
import subprocess
import tempfile
import time
import unittest

import aiohttp
from aiohttp import web
from waveshare_bridge import live_bridge as bridge
from tests.support import authorizer

ROOT = pathlib.Path(__file__).parent


def lan_ip():
    """This host's RFC1918 IPv4 (WAVESHARE_AI_TEST_LAN_IP overrides), or None. No packet is sent."""
    import ipaddress, os, socket
    value = os.environ.get('WAVESHARE_AI_TEST_LAN_IP')
    if not value:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            try:
                s.connect(('192.0.2.1', 9))  # TEST-NET-1: route lookup only
                value = s.getsockname()[0]
            except OSError:
                return None
    ip = ipaddress.IPv4Address(value)
    return value if ip.is_private and not ip.is_loopback else None
TOKEN = '62' * 32
KEY = b'k' * 32


async def serve(app, context=None):
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    site = web.TCPSite(runner, '127.0.0.1', 0, ssl_context=context)
    await site.start()
    port = site._server.sockets[0].getsockname()[1]
    return runner, ('https' if context else 'http') + f'://127.0.0.1:{port}'


class HTTPSTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        cls.cert = pathlib.Path(cls.tmp.name) / 'cert.pem'
        cls.key = pathlib.Path(cls.tmp.name) / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(cls.key), '-out', str(cls.cert), '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=IP:127.0.0.1'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        cls.key.chmod(0o600)
        cls.server_tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        cls.server_tls.load_cert_chain(cls.cert, cls.key)
        cls.client_tls = ssl.create_default_context(cafile=str(cls.cert))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    async def test_bridge_lifecycle_uses_config_run_tls_and_shutdown(self):
        from waveshare_bridge import cli, config, enroll
        lan = lan_ip()
        if lan is None:
            self.skipTest('no RFC1918 interface')
        import socket
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / 'board.key').write_text('12' * 32)
            (root / 'board.key').chmod(0o600)
            (root / 'tls.crt').write_bytes(self.cert.read_bytes())
            (root / 'tls.key').write_bytes(self.key.read_bytes())
            (root / 'tls.key').chmod(0o600)
            with socket.socket() as probe:
                probe.bind((lan, 0))
                port = probe.getsockname()[1]
            enroll.write_private_json(root / 'bridge.json', {
                'bind': lan, 'port': port, 'mdns': False, 'providers': {}})
            registry = enroll.Registry(root / 'boards.json')
            registry.add(bytes.fromhex(TOKEN), 'Test board')
            runtime = await cli.start(config.load(root))
            try:
                self.assertFalse(runtime.inner.task.done())
                async with aiohttp.ClientSession() as client:
                    async with client.get(f'https://{lan}:{runtime.port}/v1/live',
                            ssl=aiohttp.Fingerprint(__import__('waveshare_bridge.enroll', fromlist=['']).cert_fingerprint(self.cert)),
                            headers={'Authorization': 'Bearer ' + TOKEN}) as response:
                        self.assertEqual(response.status, 404)  # no tiles configured
            finally:
                await runtime.close()
            self.assertTrue(runtime.inner.task.done())
            self.assertTrue(runtime.inner.client.closed)

    async def test_poll_loop_one_second_cadence_and_cancel(self):
        from waveshare_bridge.common import new_client
        moments = []
        async def usage(request):
            moments.append(time.monotonic())
            return web.json_response({'version': 1, 'sessions': []})
        app = web.Application()
        app.router.add_get(bridge.USAGE_ROUTE, usage)
        runner, base = await serve(app)
        class Credentials:
            expires_at = float('inf')
            async def token(self): return 'fixture-access'
        try:
            async with new_client() as client:
                sample = bridge.Sample(KEY)
                gateway = bridge.UsageGateway(base, Credentials(), sample, client)
                task = asyncio.create_task(gateway.run())
                try:
                    for _ in range(150):
                        if len(moments) >= 2: break
                        await asyncio.sleep(0.02)
                    self.assertGreaterEqual(len(moments), 2)
                    self.assertGreaterEqual(moments[1] - moments[0], 0.95)
                finally:
                    task.cancel()
                    with self.assertRaises(asyncio.CancelledError): await task
                    self.assertIsNone(sample.body)
        finally:
            await runner.cleanup()

    async def test_authenticated_gateway_fixture_to_https(self):
        from waveshare_bridge import common, authelia_client
        from tests.test_authelia_client import FakeIdP, client_config, ACCESS
        idp = FakeIdP()
        mode = {'rows': []}
        calls = []
        async def usage(request):
            calls.append((request.path, request.headers.get('Authorization')))
            if mode['rows'] == 'malformed': return web.Response(text='{', content_type='application/json')
            if mode['rows'] == 'failure': return web.Response(status=503)
            return web.json_response({'version': 1, 'sessions': mode['rows']})
        upstream = web.Application()
        upstream.router.add_post('/api/oidc/token', idp.token)
        upstream.router.add_get(bridge.USAGE_ROUTE, usage)
        runner, base = await serve(upstream)
        sample = bridge.Sample(KEY)
        downstream, url = await serve(bridge.make_app(sample, authorizer(TOKEN)), self.server_tls)
        try:
            async with common.new_client() as client:
                credentials = authelia_client.ClientCredentials(client_config(base + '/api/oidc/token'), client)
                gateway = bridge.UsageGateway(base, credentials, sample, client)
                for rows, status, count in [([], 200, 0),
                    ([{'id': 'a', 'status': 'working', 'title': 'secret'},
                      {'id': 'b', 'status': 'working', 'preview': 'secret'},
                      {'id': 'c', 'status': 'waiting'}], 200, 3),
                    ('malformed', 503, None), ('failure', 503, None),
                    ([{'id': str(i), 'status': 'working'} for i in range(129)], 503, None)]:
                    mode['rows'] = rows
                    await gateway.poll_once()
                    async with client.get(url + '/v1/live', ssl=self.client_tls,
                            headers={'Authorization': 'Bearer ' + TOKEN}) as response:
                        self.assertEqual(response.status, status)
                        body = await response.read()
                        if count is not None:
                            self.assertEqual(body[:8], b'WLS4' + count.to_bytes(2, 'little') + b'\0\0')
                            self.assertEqual(len(body), 8 + count * 10)
                self.assertTrue(all(path == bridge.USAGE_ROUTE for path, _ in calls))
                self.assertTrue(all(value.startswith('Bearer ' + ACCESS) for _, value in calls))
                self.assertEqual(len(idp.requests), 1)
        finally:
            await downstream.cleanup()
            await runner.cleanup()

    async def test_https_auth_cache_and_read_only_routes(self):
        self.assertTrue(hasattr(bridge, 'Sample'), 'sample/HTTPS implementation missing')
        sample = bridge.Sample(KEY)
        runner, url = await serve(bridge.make_app(sample, authorizer(TOKEN)), self.server_tls)
        try:
            async with aiohttp.ClientSession() as client:
                async def request(path='/v1/live', token=TOKEN, method='GET'):
                    async with client.request(method, url + path, ssl=self.client_tls,
                            headers={'Authorization': 'Bearer ' + token}, allow_redirects=False) as r:
                        return r.status, r.headers, await r.read()
                self.assertEqual((await request(token='wrong'))[0], 401)
                self.assertEqual((await request())[0], 503)
                sample.update([])
                status, headers, body = await request()
                self.assertEqual(status, 200)
                self.assertEqual(body, b'WLS4\0\0\0\0')
                self.assertEqual(headers['Content-Type'], 'application/octet-stream')
                self.assertEqual(headers['Cache-Control'], 'no-store')
                sample.received = time.monotonic() - 3.01
                self.assertEqual((await request())[0], 503)
                sample.update([{'id': 'a', 'status': 'working'}, {'id': 'b', 'status': 'working'}])
                self.assertEqual((await request())[2], bridge.encode_sessions(
                    [{'id': 'a', 'status': 'working'}, {'id': 'b', 'status': 'working'}], KEY, 0, 0))
                for rows in ([{'id': 'x', 'status': 'bogus'}],
                             [{'id': str(i), 'status': 'working'} for i in range(129)]):
                    sample.update(rows)
                    self.assertEqual((await request())[0], 503)
                sample.update([])
                sample.invalidate()
                self.assertEqual((await request())[0], 503)
                for method, path in [('POST', '/v1/live'), ('HEAD', '/v1/live'),
                                     ('GET', '/'), ('GET', '/v1/live?x=1'), ('GET', '/api/rpc')]:
                    status, headers, _ = await request(path, method=method)
                    self.assertIn(status, (404, 405))
                    self.assertNotIn('Location', headers)
                    self.assertEqual(headers['Cache-Control'], 'no-store')
        finally:
            await runner.cleanup()
