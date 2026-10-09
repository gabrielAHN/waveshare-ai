"""Sensor tile: WHS1 frame, the reading bands, the configurable endpoint/scope, and the /v1/home route checks."""
import json
import struct
import unittest
import zlib

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from waveshare_bridge import home_sensors as hs
from waveshare_bridge import live_bridge as bridge
from tests.support import TOKEN, live_sample, authorizer

AUTH = {'Authorization': 'Bearer ' + TOKEN}
READINGS = {'temperature': {'value': 23.4, 'age_s': 4}, 'humidity': {'value': 64.0, 'age_s': 4},
            'pressure': {'value': 1013.6, 'age_s': 4}, 'pm1': {'value': 3, 'age_s': 4},
            'pm25': {'value': 40, 'age_s': 4}, 'pm10': None}


class FrameTests(unittest.TestCase):
    def test_round_trip_values_status_and_age(self):
        state, rows = hs.decode_frame(hs.encode_frame(hs.ST_OK, READINGS))
        self.assertEqual(state, hs.ST_OK)
        self.assertEqual(rows['temperature'], (23.4, hs.GOOD, 4))
        self.assertEqual(rows['humidity'], (64.0, hs.MEDIUM, 4))     # 60 < 64 <= 70
        self.assertEqual(rows['pressure'], (1013.6, hs.GOOD, 4))
        self.assertEqual(rows['pm25'], (40.0, hs.BAD, 4))            # > 35.4
        self.assertEqual(rows['pm10'], (None, hs.UNKNOWN, None))
        self.assertEqual(len(hs.encode_frame(hs.ST_OK, READINGS)), hs.FRAME_SIZE)

    def test_thresholds_match_the_documented_bands(self):
        # home_sensors.status boundaries (docs/SETUP.md), inclusive
        cases = [('temperature', 18, 0), ('temperature', 26, 0), ('temperature', 26.1, 1), ('temperature', 14.9, 2),
                 ('humidity', 30, 0), ('humidity', 70, 1), ('humidity', 70.1, 2),
                 ('pressure', 1025, 0), ('pressure', 985, 1), ('pressure', 984, 2),
                 ('pm25', 9.0, 0), ('pm25', 35.4, 1), ('pm25', 35.5, 2), ('pm1', 10, 1),
                 ('pm10', 54, 0), ('pm10', 154, 1), ('pm10', 155, 2)]
        for key, value, want in cases:
            self.assertEqual(hs.status(key, value), want, (key, value))

    def test_stale_or_malformed_readings_are_unknown(self):
        bad = {'temperature': {'value': 23.0, 'age_s': 600}, 'humidity': {'value': 'x'}, 'pressure': {'value': True},
               'pm1': {'value': 1e9}, 'pm25': {'value': 5, 'age_s': -1}, 'pm10': 'nope'}
        _, rows = hs.decode_frame(hs.encode_frame(hs.ST_OK, bad))
        self.assertEqual(rows['temperature'][:2], (None, hs.UNKNOWN))
        for key in ('humidity', 'pressure', 'pm1', 'pm10'):
            self.assertEqual(rows[key], (None, hs.UNKNOWN, None), key)
        self.assertEqual(rows['pm25'], (5.0, hs.GOOD, None))

    def test_non_ok_state_carries_no_values(self):
        for state in (hs.ST_SETUP, hs.ST_UNREACHABLE, hs.ST_REFUSED):
            got, rows = hs.decode_frame(hs.encode_frame(state, READINGS))
            self.assertEqual(got, state)
            self.assertTrue(all(v == (None, hs.UNKNOWN, None) for v in rows.values()))

    def test_crc_guards_the_frame(self):
        frame = bytearray(hs.encode_frame(hs.ST_OK, READINGS))
        frame[9] ^= 1
        with self.assertRaises(ValueError):
            hs.decode_frame(bytes(frame))
        self.assertEqual(struct.unpack('<I', bytes(frame[-4:]))[0], zlib.crc32(hs.encode_frame(hs.ST_OK, READINGS)[:-4]))


class FakeCredentials:
    def __init__(self):
        self.minted = 0
        self.invalidated = 0

    async def token(self):
        self.minted += 1
        return 'machine-token-%d' % self.minted

    def invalidate(self):
        self.invalidated += 1


GATEWAY_BODY = {'now': 1790606174, 'sensors': {
    'temperature': {'state': '25.4', 'at': 1790606170}, 'humidity': {'state': '75.1', 'at': 1790606172},
    'pressure': {'state': '995.5', 'at': 1790606172}, 'pm1': {'state': '22', 'at': 1790606160},
    'pm25': {'state': '30', 'at': 1790606116}, 'pm10': {'state': 'unavailable', 'at': 1790606138}}}


class GatewayServer:
    """A fake auth-proxy edge answering only GET /api/waveshare-sensors (forward-auth bearer check)."""

    def __init__(self, body=None, status=200, refuse_first=False):
        self.body = body if body is not None else GATEWAY_BODY
        self.status, self.refuse_first, self.requests = status, refuse_first, []

    def app(self):
        async def sensor(request):
            self.requests.append((request.method, request.path, request.query_string,
                                  request.headers.get('Authorization'), request.headers.get('Host'),
                                  request.headers.get('X-Forwarded-Proto'), request.headers.get('Cookie')))
            if self.refuse_first and len(self.requests) == 1:
                return web.Response(status=401)
            return web.json_response(self.body, status=self.status)
        app = web.Application()
        app.router.add_get('/api/waveshare-sensors', sensor)
        return app


class SourceTests(unittest.IsolatedAsyncioTestCase):
    async def source(self, gateway, host='example.com'):
        self.server = TestServer(gateway.app(), host='127.0.0.1')
        await self.server.start_server()
        self.session = aiohttp.ClientSession()
        self.creds = FakeCredentials()
        resource = {'resource_url': f'http://127.0.0.1:{self.server.port}/api/waveshare-sensors', 'resource_host_header': host}
        return hs.HomeSource(resource, self.creds, self.session, log=lambda *_: None)

    async def asyncTearDown(self):
        await self.session.close()
        await self.server.close()

    async def test_a_steady_reading_is_not_stale(self):
        # PM1 = 0 for five hours: HA's last_updated is old but the sensor is live (HA would
        # report 'unavailable' after expire_after of silence). The sensor itself shows 0,
        # so the tile must too.
        body = {'now': 1790606174, 'sensors': {
            'temperature': {'state': '25.9', 'at': 1790606174 - 441},
            'humidity': {'state': '75.4', 'at': 1790606170},
            'pressure': {'state': '997.5', 'at': 1790606170},
            'pm1': {'state': '0', 'at': 1790606174 - 18658},
            'pm25': {'state': '1', 'at': 1790606174 - 114},
            'pm10': {'state': '1', 'at': 1790606174 - 114}}}
        source = await self.source(GatewayServer(body))
        state, rows = hs.decode_frame(await source.frame())
        self.assertEqual(state, hs.ST_OK)
        self.assertEqual({k: v[0] for k, v in rows.items()},
                         {'temperature': 25.9, 'humidity': 75.4, 'pressure': 997.5,
                          'pm1': 0.0, 'pm25': 1.0, 'pm10': 1.0})

    async def test_reads_the_gateway_route_with_the_bearer_token(self):
        gateway = GatewayServer()
        source = await self.source(gateway)
        state, rows = hs.decode_frame(await source.frame())
        self.assertEqual(state, hs.ST_OK)
        self.assertEqual(rows['temperature'], (25.4, hs.GOOD, None))
        self.assertEqual(rows['humidity'], (75.1, hs.BAD, None))
        self.assertEqual(rows['pressure'], (995.5, hs.MEDIUM, None))
        self.assertEqual(rows['pm25'], (30.0, hs.MEDIUM, None))
        self.assertEqual(rows['pm10'], (None, hs.UNKNOWN, None))       # 'unavailable'
        self.assertEqual(gateway.requests,
                         [('GET', '/api/waveshare-sensors', '', 'Bearer machine-token-1', 'example.com', 'https', None)])
        await source.frame()
        self.assertEqual(len(gateway.requests), 1)     # cached

    async def test_refused_token_is_retried_once_fresh_then_reported(self):
        gateway = GatewayServer(refuse_first=True)
        source = await self.source(gateway)
        self.assertEqual(hs.decode_frame(await source.frame())[0], hs.ST_OK)
        self.assertEqual((self.creds.invalidated, [r[3] for r in gateway.requests]),
                         (1, ['Bearer machine-token-1', 'Bearer machine-token-2']))
        await self.asyncTearDown()
        source = await self.source(GatewayServer(status=401))
        self.assertEqual(hs.decode_frame(await source.frame())[0], hs.ST_REFUSED)

    async def test_bad_or_down_gateway_is_unreachable(self):
        for gateway in (GatewayServer(status=503), GatewayServer(body={'available': False}),
                        GatewayServer(body={'now': 'x', 'sensors': {}})):
            source = await self.source(gateway)
            self.assertEqual(hs.decode_frame(await source.frame())[0], hs.ST_UNREACHABLE)
            await self.asyncTearDown()
        self.server = TestServer(web.Application(), host='127.0.0.1')
        await self.server.start_server()
        self.session = aiohttp.ClientSession()


class ClientFileTests(unittest.TestCase):
    def write(self, value):
        import os
        import tempfile
        d = tempfile.mkdtemp()
        path = os.path.join(d, 'home-client.json')
        with open(path, 'w') as f:
            json.dump(value, f)
        os.chmod(path, 0o600)
        return path

    BASE = {'token_endpoint': 'http://127.0.0.1/api/oidc/token', 'token_host_header': 'auth.example.com',
            'client_id': 'waveshare-home', 'client_secret': 's' * 64, 'scope': 'authelia.bearer.authz',
            'audience': 'https://example.com/api/waveshare-sensors',
            'resource_url': 'http://127.0.0.1/api/waveshare-sensors', 'resource_host_header': 'example.com'}

    def test_valid_file(self):
        config, resource = hs.load_home_client(self.write(self.BASE))
        self.assertEqual((config['client_id'], config['scope']), ('waveshare-home', 'authelia.bearer.authz'))
        self.assertEqual(resource, {'resource_url': 'http://127.0.0.1/api/waveshare-sensors', 'resource_host_header': 'example.com'})

    def test_refuses_other_scopes_paths_and_hosts(self):
        for change in ({'scope': 'hermes.sessions.read'}, {'scope': 'openid'}, {'scope': 'ha.read offline_access'},
                       {'scope': 'ha read'.replace(' ', '\t')}, {'scope': 'a  b'}, {'scope': ' '.join(['s%d' % i for i in range(9)])},
                       {'scope': 7}, {'command_scope': 'x.y'},
                       {'resource_url': 'http://10.0.0.5/api/waveshare-sensors'}, {'resource_url': 'https://example.com'},
                       {'resource_url': 'https://example.com/'}, {'resource_url': 'ftp://example.com/api/x'},
                       {'resource_url': 'https://user:pw@example.com/api/x'}, {'resource_url': 'https://example.com/api/x#f'},
                       {'resource_url': 'https://example.com/api/waveshare-sensors?x=1'}, {'resource_host_header': 'a b'}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                hs.load_home_client(self.write({**self.BASE, **change}))

    def test_endpoint_path_and_host_are_configuration(self):
        # No path or hostname is built in: any https URL (or the proxy's own loopback) works.
        for url in ('https://ha.example.com/api/waveshare-sensors', 'https://example.com/sensors/living-room.json',
                    'http://127.0.0.1:8080/api/template-proxy'):
            with self.subTest(url=url):
                _, resource = hs.load_home_client(self.write({**self.BASE, 'resource_url': url}))
                self.assertEqual(resource['resource_url'], url)

    def test_bearer_scope_defaults_to_authelia_and_is_configurable(self):
        from waveshare_bridge.authelia_client import BEARER_SCOPE
        no_scope = {k: v for k, v in self.BASE.items() if k != 'scope'}
        self.assertEqual(hs.load_home_client(self.write(no_scope))[0]['scope'], BEARER_SCOPE)
        self.assertEqual(hs.load_home_client(self.write({**self.BASE, 'scope': ''}))[0]['scope'], BEARER_SCOPE)
        for scope in ('ha.sensors.read', 'sensors:read api://waveshare/read', 'authelia.bearer.authz extra.scope'):
            with self.subTest(scope=scope):
                self.assertEqual(hs.load_home_client(self.write({**self.BASE, 'scope': scope}))[0]['scope'], scope)

    def test_bearer_scope_is_not_accepted_for_the_hermes_client(self):
        from waveshare_bridge.authelia_client import validate_client
        base = {k: v for k, v in self.BASE.items() if not k.startswith('resource')}
        with self.assertRaises(ValueError):
            validate_client(base)


class FakeHome:
    def __init__(self):
        self.calls = 0

    async def frame(self):
        self.calls += 1
        return hs.encode_frame(hs.ST_OK, READINGS)


class FakePhone:
    provider_names = frozenset({'hermes', 'home_assistant'})

    def __init__(self, home):
        self._home = home

    def allowed(self, board):
        return True

    def home_allowed(self, board):
        return self._home

    def status(self, board, provider='hermes', header=False):
        from waveshare_bridge import phone_pair
        return phone_pair.encode_status('none', flags=phone_pair.FLAG_REQUIRED)


class RouteTests(unittest.IsolatedAsyncioTestCase):
    async def client(self, phone):
        self.home = FakeHome()
        self.http = TestClient(TestServer(bridge.make_app(live_sample(), authorizer(TOKEN), home=self.home, phone=phone)))
        await self.http.start_server()
        return self.http

    async def asyncTearDown(self):
        await self.http.close()

    async def test_board_token_get_only_no_query(self):
        c = await self.client(FakePhone(True))
        self.assertEqual((await c.get('/v1/home')).status, 401)
        self.assertEqual((await c.get('/v1/home', headers={'Authorization': 'Bearer ' + 'd' * 64})).status, 401)
        self.assertEqual((await c.post('/v1/home', headers=AUTH)).status, 405)
        self.assertEqual((await c.get('/v1/home?entity=x', headers=AUTH)).status, 404)
        self.assertEqual(self.home.calls, 0)
        response = await c.get('/v1/home', headers=AUTH)
        self.assertEqual((response.status, response.content_type, response.headers['Cache-Control']),
                         (200, 'application/octet-stream', 'no-store'))
        self.assertEqual(hs.decode_frame(await response.read())[1]['temperature'][0], 23.4)

    async def test_board_without_a_home_sign_in_gets_nothing(self):
        c = await self.client(FakePhone(False))
        response = await c.get('/v1/home', headers=AUTH)
        self.assertEqual((response.status, await response.text()), (409, 'home_auth'))
        self.assertEqual(self.home.calls, 0)

    async def test_no_phone_sign_in_configured_means_no_home(self):
        c = await self.client(None)
        response = await c.get('/v1/home', headers=AUTH)
        self.assertEqual((response.status, await response.text()), (409, 'home_auth'))
        self.assertEqual(self.home.calls, 0)

    async def test_route_absent_without_a_source(self):
        self.http = TestClient(TestServer(bridge.make_app(live_sample(), authorizer(TOKEN))))
        await self.http.start_server()
        self.assertEqual((await self.http.get('/v1/home', headers=AUTH)).status, 404)

if __name__ == '__main__':
    unittest.main()
