"""Phone sign-in (OAuth 2.0 device authorization grant, RFC 8628) for enrolled boards.

A fake Authelia (aiohttp, loopback, no network) scripts the token endpoint so every branch of the
bridge's poll loop runs against the real HTTP code: approve, slow_down, expired, DENY (Authelia 4.39
answers 500 server_error instead of access_denied), a user outside the allowed groups, plus the
board-facing routes, phone/bots policy and revocation.
"""
import asyncio
import contextlib
import io
import json
import os
import pathlib
import struct
import tempfile
import time
import unittest
import zlib

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from waveshare_bridge import enroll
from waveshare_bridge import live_bridge as bridge
from waveshare_bridge import phone_pair
from tests.test_transport import serve
from tests.support import live_sample

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOKEN_A = bytes(range(1, 33))
TOKEN_B = bytes(range(101, 133))
PUBLIC_HOST = 'auth.example.com'
ACCESS = 'pairing-access-' + 'q' * 24


def bearer(token):
    return {'Authorization': 'Bearer ' + token.hex(), 'X-Provider': 'hermes'}


class FakeAuthelia:
    """Device-authorization, token and userinfo endpoints with a scripted token-endpoint queue."""

    def __init__(self):
        self.script = []           # list of (status, json) returned by successive token polls
        self.retry_after = None    # Retry-After header value sent with scripted 429s
        self.userinfo = {'preferred_username': 'samlee', 'name': 'Sam Lee', 'groups': ['admins']}
        self.device_status = 200
        self.expires_in = 599
        self.interval = 10
        self.requests = []
        self.polls = 0
        self.issued = 0

    def app(self):
        app = web.Application()
        app.router.add_post('/api/oidc/device-authorization', self.device)
        app.router.add_post('/api/oidc/token', self.token)
        app.router.add_get('/api/oidc/userinfo', self.info)
        return app

    async def device(self, request):
        form = dict(await request.post())
        self.requests.append(('device', form, request.headers.get('Host'), request.headers.get('Authorization')))
        if self.device_status != 200:
            return web.json_response({'error': 'invalid_client'}, status=self.device_status)
        self.issued += 1
        code = f'WXYZ-{self.issued:04d}'
        return web.json_response({
            'device_code': f'device-code-{self.issued}-' + 'd' * 20, 'user_code': code,
            'verification_uri': f'https://{PUBLIC_HOST}/consent/openid/device-authorization',
            'verification_uri_complete': f'https://{PUBLIC_HOST}/consent/openid/device-authorization?user_code={code}',
            'expires_in': self.expires_in, 'interval': self.interval})

    async def token(self, request):
        form = dict(await request.post())
        self.requests.append(('token', form, request.headers.get('Host'), request.headers.get('Authorization')))
        self.polls += 1
        if not self.script:
            return web.json_response({'error': 'authorization_pending'}, status=400)
        status, body = self.script.pop(0)
        headers = {'Retry-After': self.retry_after} if status == 429 and self.retry_after else None
        return web.json_response(body, status=status, headers=headers)

    async def info(self, request):
        self.requests.append(('userinfo', {}, request.headers.get('Host'), request.headers.get('Authorization')))
        if request.headers.get('Authorization') != 'Bearer ' + ACCESS:
            return web.json_response({'error': 'invalid_token'}, status=401)
        return web.json_response(self.userinfo)


APPROVED = (200, {'access_token': ACCESS, 'token_type': 'bearer', 'expires_in': 300, 'id_token': 'x.y.z',
                  'scope': 'openid profile groups'})
PENDING = (400, {'error': 'authorization_pending'})


def decode(frame):
    return phone_pair.decode_status(frame)


class PhoneBase(unittest.IsolatedAsyncioTestCase):
    require = True
    idp_class = FakeAuthelia
    paths = None

    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        self.registry = enroll.Registry(pathlib.Path(self.tmp.name) / 'boards.json')
        self.board_a = self.registry.add(TOKEN_A, 'Desk')
        self.board_b = self.registry.add(TOKEN_B, 'Kitchen')
        self.idp = self.idp_class()
        self.idp_runner, self.idp_base = await serve(self.idp.app())
        import aiohttp
        self.http = aiohttp.ClientSession()
        common = {'issuer_base': self.idp_base, 'client_id': 'waveshare-pairing',
                  'host_header': PUBLIC_HOST, 'public_host': PUBLIC_HOST, 'paths': self.paths}
        specs = [dict(common, provider='hermes', groups=['admins', 'hermes_users'], require=self.require),
                 dict(common, provider='home_assistant', groups=['admins'], require=True)]
        self.phone = phone_pair.PhonePairing.from_specs(self.registry, self.http, specs, time_scale=0.002)
        self.client = TestClient(TestServer(bridge.make_app(
            live_sample(), enroll.Authorizer(self.registry), phone=self.phone)))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        await self.phone.close()
        await self.http.close()
        await self.idp_runner.cleanup()
        self.tmp.cleanup()

    async def start(self, token=TOKEN_A):
        response = await self.client.post('/v1/pair/phone/start', headers=bearer(token))
        return response.status, (await response.read())

    async def status(self, token=TOKEN_A):
        response = await self.client.get('/v1/pair/phone/status', headers=bearer(token))
        self.assertEqual(response.status, 200)
        return decode(await response.read())

    async def wait_state(self, want, token=TOKEN_A, seconds=3.0):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            got = await self.status(token)
            if got['state'] == want:
                return got
            await asyncio.sleep(0.01)
        self.fail(f'state never became {want}: {got}')



class WireTests(unittest.TestCase):
    def test_frame_roundtrip_and_crc(self):
        frame = phone_pair.encode_status('pending', expires_in=598, user_code='WXYZ-0001',
                                         uri='https://auth.example.com/consent/openid/device-authorization?user_code=WXYZ-0001',
                                         flags=phone_pair.FLAG_REQUIRED)
        self.assertEqual(len(frame), phone_pair.FRAME_SIZE)
        self.assertEqual(frame[:4], b'WPH1')
        self.assertEqual(struct.unpack('<I', frame[-4:])[0], zlib.crc32(frame[:-4]))
        got = decode(frame)
        self.assertEqual((got['state'], got['expires_in'], got['user_code']), ('pending', 598, 'WXYZ-0001'))
        self.assertTrue(got['uri'].endswith('user_code=WXYZ-0001'))
        self.assertEqual(got['flags'], phone_pair.FLAG_REQUIRED)
        bad = bytearray(frame)
        bad[10] ^= 1
        with self.assertRaises(ValueError):
            decode(bytes(bad))

    def test_frame_rejects_oversized_or_unprintable_fields(self):
        for kwargs in ({'uri': 'https://a/' + 'x' * 200}, {'user_code': 'ABC\nDEF'}, {'name': 'n' * 40}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                phone_pair.encode_status('pending', **kwargs)


class FlowTests(PhoneBase):
    async def test_happy_path_marks_board_phone_authorized_without_keeping_tokens(self):
        self.idp.script = [PENDING, APPROVED]
        status, body = await self.start()
        self.assertEqual(status, 200)
        got = decode(body)
        self.assertEqual(got['state'], 'pending')
        self.assertEqual(got['user_code'], 'WXYZ-0001')
        self.assertEqual(got['uri'], f'https://{PUBLIC_HOST}/consent/openid/device-authorization?user_code=WXYZ-0001')
        self.assertLessEqual(got['expires_in'], 599)
        self.assertTrue(got['flags'] & phone_pair.FLAG_REQUIRED)
        got = await self.wait_state('authorized')
        self.assertEqual(got['name'], 'Sam Lee')
        entry = self.registry.match(TOKEN_A)
        self.assertEqual(entry['phone_user'], 'samlee')
        self.assertGreater(entry['phone_at'], 0)
        self.assertIsNone(self.registry.match(TOKEN_B).get('phone_user'))
        text = pathlib.Path(self.registry.path).read_text()
        self.assertNotIn(ACCESS, text)
        self.assertNotIn('device-code', text)
        # client_id travels in the form body (Authelia 4.39 401s "client ''" otherwise); public client.
        kinds = [r[0] for r in self.idp.requests]
        self.assertEqual(kinds[0], 'device')
        device_form = self.idp.requests[0][1]
        self.assertEqual(device_form['client_id'], 'waveshare-pairing')
        self.assertEqual(device_form['scope'], 'openid profile groups')
        token_form = [r[1] for r in self.idp.requests if r[0] == 'token'][0]
        self.assertEqual(token_form['grant_type'], 'urn:ietf:params:oauth:grant-type:device_code')
        self.assertEqual(token_form['client_id'], 'waveshare-pairing')
        self.assertTrue(all(r[2] == PUBLIC_HOST for r in self.idp.requests))
        self.assertTrue(all(r[3] is None for r in self.idp.requests if r[0] != 'userinfo'))
        self.assertTrue(self.phone.allowed(self.registry.match(TOKEN_A)))

    async def test_cli_revoke_wins_over_a_remembered_authorized_flow(self):
        self.idp.script = [APPROVED]
        await self.start()
        await self.wait_state('authorized')
        self.assertTrue(self.registry.clear_phone(self.registry.match(TOKEN_A)['id']))   # `boards --revoke-phone`
        got = decode(self.phone.status(self.registry.match(TOKEN_A)))
        self.assertEqual(got['state'], 'none')
        self.assertFalse(self.phone.allowed(self.registry.match(TOKEN_A)))

    async def test_start_on_an_authorized_board_keeps_it_authorized(self):
        # An authorized board stays authorized; start does not create another device flow.
        self.idp.script = [APPROVED]
        await self.start()
        await self.wait_state('authorized')
        requests = len(self.idp.requests)
        self.phone.min_restart = 0
        status, body = await self.start()
        self.assertEqual(status, 200)
        self.assertEqual(decode(body)['state'], 'authorized')
        self.assertEqual((await self.status())['state'], 'authorized')
        self.assertEqual(len(self.idp.requests), requests)
        self.assertTrue(self.phone.allowed(self.registry.match(TOKEN_A)))

    async def test_slow_down_increases_poll_interval(self):
        self.idp.interval = 1
        self.idp.script = [(400, {'error': 'slow_down'}), PENDING, APPROVED]
        await self.start()
        await self.wait_state('authorized')
        self.assertEqual(self.phone.flows_debug(self.board_a['id'])['interval'], 6)

    async def test_rate_limited_429_backs_off_and_keeps_polling(self):
        # Authelia rate-limits /api/oidc/token per remote IP (shared with the bridge's own client
        # credentials); a 429 must not end the sign-in.
        self.idp.interval = 1
        self.idp.script = [(429, {'status': 'KO', 'message': 'Rate Limit Exceeded'}),
                           (429, {'status': 'KO', 'message': 'Rate Limit Exceeded'}), PENDING, APPROVED]
        self.idp.retry_after = '2'
        await self.start()
        await self.wait_state('authorized')
        self.assertEqual(self.idp.polls, 4)
        self.assertGreaterEqual(self.phone.flows_debug(self.board_a['id'])['interval'], 2)

    async def test_expired_token_reports_expired(self):
        self.idp.script = [(400, {'error': 'expired_token'})]
        await self.start()
        got = await self.wait_state('expired')
        self.assertEqual(got['user_code'], '')
        self.assertIsNone(self.registry.match(TOKEN_A).get('phone_user'))

    async def test_local_deadline_expires_without_answer(self):
        self.idp.expires_in = 30       # scaled to 0.06 s: the poll loop gives up at its own deadline
        self.phone.deadline_scale = 0.002
        status, _ = await self.start()
        self.assertEqual(status, 200)
        await self.wait_state('expired')

    async def test_repeated_server_error_is_treated_as_denied(self):
        err = (500, {'error': 'server_error'})
        self.idp.script = [PENDING, err, err]
        await self.start()
        await self.wait_state('denied')
        self.assertIsNone(self.registry.match(TOKEN_A).get('phone_user'))

    async def test_single_server_error_is_retried(self):
        self.idp.script = [(500, {'error': 'server_error'}), PENDING, APPROVED]
        await self.start()
        await self.wait_state('authorized')

    async def test_access_denied(self):
        self.idp.script = [(400, {'error': 'access_denied'})]
        await self.start()
        await self.wait_state('denied')

    async def test_home_policy_checks_real_sign_in_groups(self):
        self.idp.script = [APPROVED]
        await self.start()
        got = await self.wait_state('authorized')
        board = self.registry.match(TOKEN_A)
        self.assertEqual(board['phone_groups'], ['admins'])
        self.assertEqual(got['flags'], 9)
        self.assertTrue(self.phone.home_allowed(board))
        self.registry.clear_phone(board['id'])
        self.assertFalse(self.phone.home_allowed(self.registry.match(TOKEN_A)))
        self.idp.userinfo = {'preferred_username': 'samlee', 'groups': ['hermes_users']}
        self.idp.script = [APPROVED]
        self.phone.min_restart = 0
        await self.start()
        await self.wait_state('authorized')
        board = self.registry.match(TOKEN_A)
        self.assertTrue(self.phone.allowed(board))
        self.assertFalse(self.phone.home_allowed(board))
        self.assertFalse(self.phone.home_allowed(None))

    async def test_user_outside_allowed_groups_is_refused(self):
        self.idp.userinfo = {'preferred_username': 'guest', 'name': 'Guest', 'groups': ['family']}
        self.idp.script = [APPROVED]
        await self.start()
        got = await self.wait_state('refused')
        self.assertEqual(got['name'], '')
        self.assertIsNone(self.registry.match(TOKEN_A).get('phone_user'))

    async def test_authelia_unreachable_or_rejecting_client(self):
        self.idp.device_status = 401
        status, body = await self.start()
        self.assertEqual(status, 503)
        self.assertEqual((await self.status())['state'], 'none')

    async def test_restart_replaces_flow_and_is_rate_limited(self):
        await self.start()
        status, _ = await self.start()
        self.assertEqual(status, 429)
        self.phone.min_restart = 0
        status, body = await self.start()
        self.assertEqual(status, 200)
        self.assertEqual(decode(body)['user_code'], 'WXYZ-0002')

    async def test_routes_need_board_token_and_exact_methods(self):
        for method, path in (('POST', '/v1/pair/phone/start'), ('GET', '/v1/pair/phone/status'),
                             ('POST', '/v1/pair/phone/forget')):
            response = await self.client.request(method, path, headers={'Authorization': 'Bearer ' + 'e' * 64, 'X-Provider': 'hermes'})
            self.assertEqual(response.status, 401)
            wrong = 'GET' if method == 'POST' else 'POST'
            response = await self.client.request(wrong, path, headers=bearer(TOKEN_A))
            self.assertEqual(response.status, 405)
        self.assertEqual(self.idp.requests, [])

    async def test_no_code_token_or_name_in_logs(self):
        self.idp.script = [APPROVED]
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            await self.start()
            await self.wait_state('authorized')
        text = out.getvalue()
        for secret in (ACCESS, 'WXYZ-0001', 'device-code', 'Sam Lee', 'samlee'):
            self.assertNotIn(secret, text)
        self.assertIn('Phone sign-in', text)

    async def test_failure_log_names_the_oauth_error_only(self):
        self.idp.script = [PENDING, (400, {'error': 'invalid_grant', 'error_description': 'device-code gone'})]
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            await self.start()
            await self.wait_state('error')
        text = out.getvalue()
        self.assertIn('Phone sign-in error for board', text)
        self.assertIn('(http 400 invalid_grant)', text)
        self.assertNotIn('device-code', text)


class GateTests(PhoneBase):
    async def test_phone_policy_and_live_feed_before_and_after_approval(self):
        self.assertFalse(self.phone.allowed(self.registry.match(TOKEN_A)))
        self.assertEqual((await self.client.get('/v1/live', headers=bearer(TOKEN_A))).status, 200)
        self.idp.script = [APPROVED]
        await self.start()
        await self.wait_state('authorized')
        self.assertTrue(self.phone.allowed(self.registry.match(TOKEN_A)))
        self.assertFalse(self.phone.allowed(self.registry.match(TOKEN_B)))

    async def test_revoke_via_route_and_registry(self):
        self.idp.script = [APPROVED]
        await self.start()
        await self.wait_state('authorized')
        response = await self.client.post('/v1/pair/phone/forget', headers=bearer(TOKEN_A))
        self.assertEqual(response.status, 200)
        self.assertEqual(decode(await response.read())['state'], 'none')
        self.assertFalse(self.phone.allowed(self.registry.match(TOKEN_A)))
        self.registry.set_phone(self.board_a['id'], 'samlee', 'Sam Lee', groups=['admins'])
        self.assertTrue(self.phone.allowed(self.registry.match(TOKEN_A)))
        self.registry.clear_phone(self.board_a['id'])
        self.assertFalse(self.phone.allowed(self.registry.match(TOKEN_A)))

    async def test_bots_frame_marks_every_bot_phone_auth_when_gated(self):
        from waveshare_bridge import bots

        class Source:
            def frame(self):
                return bots.encode_frame([{'id': 'helper', 'name': 'Helper', 'available': True, 'reason': 'none'}], 0)
        self.client_bots = TestClient(TestServer(bridge.make_app(
            live_sample(), enroll.Authorizer(self.registry), phone=self.phone, bots=Source())))
        await self.client_bots.start_server()
        try:
            response = await self.client_bots.get('/v1/bots', headers=bearer(TOKEN_A))
            got = bots.decode_frame(await response.read())
            self.assertEqual([(b['available'], b['reason']) for b in got['bots']], [(False, 'phone_auth')])
            self.registry.set_phone(self.board_a['id'], 'samlee', 'Sam Lee', groups=['admins'])
            response = await self.client_bots.get('/v1/bots', headers=bearer(TOKEN_A))
            got = bots.decode_frame(await response.read())
            self.assertEqual([(b['available'], b['reason']) for b in got['bots']], [(True, 'none')])
        finally:
            await self.client_bots.close()


class NotRequiredTests(PhoneBase):
    require = False

    async def test_not_required_allows_bots_but_flow_still_works(self):
        self.assertTrue(self.phone.allowed(self.registry.match(TOKEN_A)))
        got = await self.status()
        self.assertFalse(got['flags'] & phone_pair.FLAG_REQUIRED)
        self.idp.script = [APPROVED]
        await self.start()
        await self.wait_state('authorized')


class RegistryTests(unittest.TestCase):
    def test_phone_fields_persist_validate_and_clear(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            path = pathlib.Path(tmp) / 'boards.json'
            reg = enroll.Registry(path)
            entry = reg.add(TOKEN_A, 'Desk')
            self.assertTrue(reg.set_phone(entry['id'], 'samlee', 'Sam Lee'))
            again = enroll.Registry(path).match(TOKEN_A)
            self.assertEqual((again['phone_user'], again['phone_name']), ('samlee', 'Sam Lee'))
            self.assertEqual(oct(os.stat(path).st_mode & 0o777), '0o600')
            self.assertTrue(reg.clear_phone(entry['id']))
            self.assertIsNone(enroll.Registry(path).match(TOKEN_A).get('phone_user'))
            self.assertFalse(reg.clear_phone('nope'))
            reg.set_phone(entry['id'], 'x' * 200, 'y\n' * 50)
            clean = enroll.Registry(path).match(TOKEN_A)
            self.assertLessEqual(len(clean['phone_user']), 64)
            self.assertNotIn('\n', clean['phone_name'])
            # re-adding the same board (re-enroll) starts unauthorized
            reg.add(TOKEN_A, 'Desk')
            self.assertIsNone(enroll.Registry(path).match(TOKEN_A).get('phone_user'))


class ConfigTests(unittest.TestCase):
    def test_endpoints_derive_from_client_and_provider_settings(self):
        client = {'token_endpoint': 'https://sso.example.com/api/oidc/token'}
        got = phone_pair.settings(client, {'providers': {'hermes': {'sign_in': {
            'required': False, 'groups': ['family'], 'client_id': 'board-login'}}}})
        self.assertEqual((got['issuer_base'], got['require'], got['groups'], got['client_id']),
                         ('https://sso.example.com', False, ['family'], 'board-login'))
        for bad in ({'groups': []}, {'groups': 'admins'}, {'required': 'yes'}, {'client_id': 'a b'}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                phone_pair.settings(client, {'providers': {'hermes': {'sign_in': bad}}})


if __name__ == '__main__':
    unittest.main()


class OtherProviderIdP(FakeAuthelia):
    """The same scripted IdP under Keycloak-style endpoint paths."""
    PREFIX = '/realms/home/protocol/openid-connect'

    def app(self):
        app = web.Application()
        app.router.add_post(self.PREFIX + '/auth/device', self.device)
        app.router.add_post(self.PREFIX + '/token', self.token)
        app.router.add_get(self.PREFIX + '/userinfo', self.info)
        return app


class OtherProviderPathTests(PhoneBase):
    """bridge.json ``oidc_paths``: the phone sign-in works against a provider whose endpoints are not
    Authelia's /api/oidc/* (no site or provider is built in)."""
    idp_class = OtherProviderIdP
    paths = phone_pair.oidc_paths({'oidc_paths': {
        'device_authorization': OtherProviderIdP.PREFIX + '/auth/device',
        'token': OtherProviderIdP.PREFIX + '/token',
        'userinfo': OtherProviderIdP.PREFIX + '/userinfo'}})

    async def test_device_grant_uses_the_configured_paths(self):
        self.idp.script = [PENDING, APPROVED]
        status, body = await self.start()
        self.assertEqual(status, 200)
        self.assertEqual(decode(body)['state'], 'pending')
        got = await self.wait_state('authorized')
        self.assertEqual(got['name'], 'Sam Lee')
        self.assertEqual(self.registry.match(TOKEN_A)['phone_user'], 'samlee')
        self.assertEqual([r[0] for r in self.idp.requests], ['device', 'token', 'token', 'userinfo'])

    def test_paths_are_validated(self):
        self.assertEqual(phone_pair.oidc_paths({}), phone_pair.DEFAULT_OIDC_PATHS)
        self.assertEqual(phone_pair.oidc_paths({'oidc_paths': {'token': '/o/token'}})['userinfo'],
                         phone_pair.DEFAULT_OIDC_PATHS['userinfo'])
        for bad in ('x', {'issuer': '/x'}, {'token': 'https://evil.example/token'}, {'token': '//evil.example/t'},
                    {'token': '/a/../token'}, {'token': '/token?x=1'}, {'token': 7}, {'userinfo': ''}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                phone_pair.oidc_paths({'oidc_paths': bad})
