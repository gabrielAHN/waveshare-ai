"""Authelia client_credentials upstream mode (no interactive login, no refresh token)."""
import asyncio
import base64
import contextlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from urllib.parse import parse_qs, unquote

from aiohttp import web
from waveshare_bridge import authelia_client as ac
from waveshare_bridge import live_bridge as bridge
from waveshare_bridge import common as auth
from tests.test_transport import ROOT, serve

SECRET = 'S3cr3t-' + 'x' * 40
ACCESS = 'eyJhbGciOiJSUzI1NiJ9.' + 'a' * 60 + '.sig-' + 'z' * 30
ROUTE = '/api/plugins/waveshare-sessions/usage'


def write_private(path, value):
    path.write_text(json.dumps(value) if not isinstance(value, str) else value)
    os.chmod(path, 0o600)
    return path


def client_config(token_endpoint, **overrides):
    value = {'token_endpoint': token_endpoint, 'client_id': 'waveshare-sessions',
             'client_secret': SECRET, 'scope': 'hermes.sessions.read',
             'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions'}
    value.update(overrides)
    return {k: v for k, v in value.items() if v is not None}


class FakeIdP:
    def __init__(self):
        self.requests = []
        self.status = 200
        self.expires_in = 3600
        self.issued = 0

    async def token(self, request):
        form = await request.post()
        self.requests.append({'auth': request.headers.get('Authorization', ''), 'form': dict(form),
                              'host': request.headers.get('Host')})
        if self.status != 200:
            return web.json_response({'error': 'invalid_client'}, status=self.status)
        self.issued += 1
        return web.json_response({'access_token': f'{ACCESS}{self.issued}', 'token_type': 'bearer',
                                  'expires_in': self.expires_in, 'scope': 'hermes.sessions.read'})


class ConfigTests(unittest.TestCase):
    def test_client_file_validation(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            path = Path(tmp) / 'client.json'
            write_private(path, client_config('https://auth.example.test/api/oidc/token'))
            self.assertEqual(ac.load_client_file(path)['client_id'], 'waveshare-sessions')
            write_private(path, client_config('http://127.0.0.1/api/oidc/token',
                                              token_host_header='auth.example.com'))
            self.assertEqual(ac.load_client_file(path)['token_host_header'], 'auth.example.com')
            for bad in (client_config('http://auth.example.test/api/oidc/token'),
                        client_config('https://user:pw@auth.example.test/t'),
                        client_config('https://auth.example.test/t', client_secret='short'),
                        client_config('https://auth.example.test/t', scope='openid'),
                        client_config('https://auth.example.test/t', scope='offline_access'),
                        client_config('https://auth.example.test/t', audience=None),
                        client_config('https://auth.example.test/t', token_host_header='a/b'),
                        client_config('https://auth.example.test/t', extra='nope')):
                write_private(path, bad)
                with self.assertRaises(ValueError):
                    ac.load_client_file(path)
            write_private(path, client_config('https://auth.example.test/t'))
            os.chmod(path, 0o644)
            with self.assertRaises(ValueError):
                ac.load_client_file(path)

    def test_repr_never_contains_secret(self):
        creds = ac.ClientCredentials(client_config('https://auth.example.test/t'), client=None)
        self.assertNotIn(SECRET, repr(creds))
        self.assertNotIn(SECRET, str(creds.__dict__.get('_public', '')))


class CredentialTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.idp = FakeIdP()
        app = web.Application()
        app.router.add_post('/api/oidc/token', self.idp.token)
        self.runner, self.base = await serve(app)
        self.client = auth.new_client()
        self.now = [1_000_000.0]

    async def asyncTearDown(self):
        await self.client.close()
        await self.runner.cleanup()

    def creds(self, **overrides):
        return ac.ClientCredentials(client_config(self.base + '/api/oidc/token', **overrides),
                                    self.client, clock=lambda: self.now[0])

    async def test_client_credentials_grant_basic_auth_and_cache(self):
        creds = self.creds(token_host_header='auth.example.com')
        token = await creds.token()
        self.assertEqual(token, ACCESS + '1')
        request = self.idp.requests[0]
        self.assertEqual(request['form'], {'grant_type': 'client_credentials', 'scope': 'hermes.sessions.read',
                                           'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions'})
        self.assertEqual(request['host'], 'auth.example.com')
        scheme, value = request['auth'].split(' ', 1)
        self.assertEqual(scheme, 'Basic')
        user, password = base64.b64decode(value).decode().split(':', 1)
        self.assertEqual((unquote(user), unquote(password)), ('waveshare-sessions', SECRET))
        self.assertNotIn('client_secret', request['form'])
        for _ in range(5):
            self.assertEqual(await creds.token(), ACCESS + '1')
        self.assertEqual(len(self.idp.requests), 1)
        self.assertEqual(creds.expires_at, self.now[0] + 3600)

    async def test_bearer_client_requests_its_configured_scopes(self):
        # A forward-auth bearer client (home-client.json): Authelia's scope by default, or the scope(s)
        # the operator's own provider expects, sent verbatim as the OAuth2 scope parameter.
        for scope, want in ((None, ac.BEARER_SCOPE), ('', ac.BEARER_SCOPE),
                            ('ha.sensors.read sensors:read', 'ha.sensors.read sensors:read')):
            with self.subTest(scope=scope):
                config = ac.validate_client(client_config(self.base + '/api/oidc/token', scope=scope),
                                            scope_rule='bearer')
                await ac.ClientCredentials(config, self.client, clock=lambda: self.now[0]).token()
                self.assertEqual(self.idp.requests[-1]['form']['scope'], want)
        for scope in ('hermes.sessions.read', 'openid', 'ha.read offline_access'):
            with self.subTest(scope=scope), self.assertRaises(ValueError):
                ac.validate_client(client_config(self.base + '/api/oidc/token', scope=scope), scope_rule='bearer')

    async def test_refresh_sixty_seconds_before_expiry(self):
        creds = self.creds()
        await creds.token()
        self.now[0] += 3600 - 61
        self.assertEqual(await creds.token(), ACCESS + '1')
        self.now[0] += 2
        self.assertEqual(await creds.token(), ACCESS + '2')
        self.assertEqual(len(self.idp.requests), 2)

    async def test_invalidate_forces_new_token(self):
        creds = self.creds()
        await creds.token()
        creds.invalidate()
        self.assertEqual(await creds.token(), ACCESS + '2')

    async def test_rejection_backoff_doubles_and_caps(self):
        self.idp.status = 401
        creds = self.creds()
        delays = []
        for _ in range(9):
            with self.assertRaises(ac.ClientRejected):
                await creds.token()
            before = len(self.idp.requests)
            with self.assertRaises(ac.ClientRejected):
                await creds.token()  # inside backoff: no request
            self.assertEqual(len(self.idp.requests), before)
            delays.append(creds.retry_at - self.now[0])
            self.now[0] = creds.retry_at
        self.assertEqual(delays[:4], [5, 10, 20, 40])
        self.assertEqual(max(delays), ac.MAX_BACKOFF)
        self.idp.status = 200
        self.assertTrue((await creds.token()).startswith(ACCESS))
        self.idp.status = 401
        creds.invalidate()
        with self.assertRaises(ac.ClientRejected):
            await creds.token()
        self.assertEqual(creds.retry_at - self.now[0], 5)  # success reset the backoff

    async def test_unreachable_endpoint_is_unavailable_with_backoff(self):
        creds = ac.ClientCredentials(client_config('http://127.0.0.1:1/api/oidc/token'), self.client,
                                     clock=lambda: self.now[0])
        with self.assertRaises(auth.Unavailable):
            await creds.token()
        self.assertGreater(creds.retry_at, self.now[0])

    async def test_malformed_token_responses_rejected(self):
        creds = self.creds()
        for body in ({'access_token': 'a b', 'token_type': 'bearer', 'expires_in': 60},
                     {'access_token': ACCESS, 'token_type': 'mac', 'expires_in': 60},
                     {'access_token': ACCESS, 'token_type': 'bearer', 'expires_in': 0},
                     {'access_token': ACCESS, 'token_type': 'bearer'},
                     {'access_token': ACCESS, 'token_type': 'bearer', 'expires_in': 60, 'refresh_token': 'r'}):
            with self.subTest(body=body):
                self.assertIsNone(ac.parse_token_response(body, 0))


def usage_body(rows):
    working = [r for r in rows if r['status'] == 'working']
    return {'version': 1, 'generated_at': 1, 'sessions': rows,
            'aggregate': {'sessions': len(rows), 'working': len(working)}}


class UsageGatewayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.idp = FakeIdP()
        self.paths = []
        self.route_status = 200
        self.tokens = {'h1': 1000, 'h2': 50}
        self.seen_auth = []

        async def usage(request):
            self.paths.append(request.path_qs)
            self.seen_auth.append(request.headers.get('Authorization'))
            if self.route_status != 200:
                return web.Response(status=self.route_status)
            with_tokens = request.query.get('tokens') == '1'
            rows = [{'id': 'h1', 'status': 'working', 'working': True,
                     'provider': 'anthropic' if with_tokens else None, 'title': 'SECRET'},
                    {'id': 'h2', 'status': 'working', 'working': True, 'provider': None},
                    {'id': 'h3', 'status': 'idle', 'working': False}]
            if with_tokens:
                for row in rows:
                    if row['status'] == 'working':
                        row['tokens'] = self.tokens[row['id']]
            return web.json_response(usage_body(rows))

        app = web.Application()
        app.router.add_post('/api/oidc/token', self.idp.token)
        app.router.add_get(ROUTE, usage)
        self.runner, self.base = await serve(app)
        self.client = auth.new_client()
        self.creds = ac.ClientCredentials(client_config(self.base + '/api/oidc/token'), self.client)
        self.sample = bridge.Sample(b'k' * 32)
        self.gateway = bridge.UsageGateway(self.base, self.creds, self.sample, self.client, usage_interval=2)

    async def asyncTearDown(self):
        await self.client.close()
        await self.runner.cleanup()

    async def test_poll_emits_wls4_and_only_uses_usage_route(self):
        self.assertTrue(await self.gateway.poll_once())
        self.assertEqual(self.sample.body[:4], b'WLS4')
        self.assertEqual(int.from_bytes(self.sample.body[4:6], 'little'), 3)  # every open session
        self.assertEqual(self.sample.rows, [{'id': 'h1', 'status': 'working', 'provider': 'anthropic'},
                                            {'id': 'h2', 'status': 'working', 'provider': None},
                                            {'id': 'h3', 'status': 'idle', 'provider': None}])
        self.assertTrue(all(p.split('?')[0] == ROUTE for p in self.paths))
        self.assertTrue(all(a == 'Bearer ' + ACCESS + '1' for a in self.seen_auth))
        self.assertEqual(self.sample.auth_expires, self.creds.expires_at)
        self.assertEqual(self.gateway.state, 'ready')

    async def test_token_sweep_measures_density(self):
        self.gateway.usage_interval = 0.0
        self.assertTrue(await self.gateway.poll_once())
        self.assertIn('tokens=1', self.paths[-1])
        await asyncio.sleep(0.05)
        self.tokens = {'h1': 1000 + 50_000, 'h2': 50}
        self.assertTrue(await self.gateway.poll_once())
        level, flags = self.sample.tracker.wire()
        self.assertTrue(flags & bridge.FLAG_MEASURED)
        self.assertEqual(level, 1)  # 50k tokens in the 60 s window = 50k tok/min: the low range
        self.assertEqual(self.sample.body[6], 1)

    async def test_roster_only_between_sweeps(self):
        self.gateway.usage_interval = 60
        await self.gateway.poll_once()
        await self.gateway.poll_once()
        self.assertEqual(['tokens=1' in p for p in self.paths], [True, False])
        self.assertEqual(self.sample.rows[0]['provider'], 'anthropic')

    async def test_route_401_refetches_token_once_then_waits_for_plugin(self):
        self.route_status = 401
        self.assertFalse(await self.gateway.poll_once())
        self.assertIsNone(self.sample.body)
        self.assertEqual(self.gateway.state, 'waiting-plugin')
        self.assertEqual(self.idp.issued, 2)  # stale-token retry, then give up
        self.route_status = 200
        self.assertTrue(await self.gateway.poll_once())
        self.assertEqual(self.gateway.state, 'ready')

    async def test_route_404_is_waiting_for_plugin(self):
        self.route_status = 404
        self.assertFalse(await self.gateway.poll_once())
        self.assertEqual(self.gateway.state, 'waiting-plugin')

    async def test_rejected_client_is_waiting_for_authelia(self):
        self.idp.status = 401
        self.assertFalse(await self.gateway.poll_once())
        self.assertEqual(self.gateway.state, 'waiting-authelia')
        self.assertEqual(self.paths, [])

    async def test_route_503_invalidates_sample(self):
        await self.gateway.poll_once()
        self.route_status = 503
        self.assertFalse(await self.gateway.poll_once())
        self.assertIsNone(self.sample.body)
        self.assertEqual(self.gateway.state, 'unavailable')


class CLITests(unittest.IsolatedAsyncioTestCase):

    async def test_waiting_state_logged_without_secret_or_token(self):
        idp = FakeIdP()
        idp.status = 401
        app = web.Application()
        app.router.add_post('/api/oidc/token', idp.token)
        from tests.test_transport import lan_ip
        lan = lan_ip()
        if lan is None:
            self.skipTest('no RFC1918 interface on this host')
        runner, base = await serve(app)
        cert_dir = tempfile.TemporaryDirectory(dir=ROOT)
        process = None
        import socket
        with socket.socket() as probe:
            probe.bind((lan, 0))
            port = probe.getsockname()[1]
        try:
            root = Path(cert_dir.name)
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout',
                            str(root / 'k.pem'), '-out', str(root / 'c.pem'), '-days', '1', '-subj', '/CN=x'],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.chmod(root / 'k.pem', 0o600)
            write_private(root / 'board.key', '12' * 32)
            write_private(root / 'authelia-client.json', client_config(base + '/api/oidc/token'))
            from waveshare_bridge import enroll
            (root / 'tls.crt').write_bytes((root / 'c.pem').read_bytes())
            (root / 'tls.key').write_bytes((root / 'k.pem').read_bytes())
            (root / 'tls.key').chmod(0o600)
            enroll.write_private_json(root / 'bridge.json', {'bind': lan, 'port': port, 'mdns': False,
                'providers': {'hermes': {'gateway': 'http://127.0.0.1:1', 'quota_cache': False, 'profiles_dir': str(root / 'profiles')}}})
            process = await asyncio.create_subprocess_exec(
                sys.executable, '-m', 'waveshare_bridge.cli', '--config-dir', str(root), 'run',
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            await asyncio.sleep(2.5)
            idp.status = 200  # client accepted; dashboard route unreachable -> unavailable, token issued
            await asyncio.sleep(7)
            process.terminate()
            out, err = await asyncio.wait_for(process.communicate(), 10)
            text = (out + err).decode()
            self.assertIn('waiting for the sign-in provider client', text)
            self.assertNotIn(SECRET, text)
            self.assertNotIn(ACCESS, text)
            self.assertNotIn('Traceback', text)
            self.assertGreaterEqual(len(idp.requests), 2)
        finally:
            if process is not None and process.returncode is None:
                process.kill()
            await runner.cleanup()
            cert_dir.cleanup()
