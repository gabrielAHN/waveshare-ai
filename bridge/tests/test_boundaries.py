"""Current HTTP upstream boundaries, privacy and enrolled-board authorization."""
import asyncio
import time
import unittest
import aiohttp
from aiohttp import web
from waveshare_bridge import common as auth, live_bridge as bridge
from tests.test_transport import serve
from tests.support import authorizer, TOKEN


class BoundaryTests(unittest.IsolatedAsyncioTestCase):
    async def test_upstream_size_duplicate_nan_timeout_redirect_and_recovery(self):
        reached = []
        mode = ['valid']
        async def respond(request):
            if mode[0] == 'redirect': return web.Response(status=302, headers={'Location': '/sink'})
            if mode[0] == 'slow':
                await asyncio.sleep(3.2)
                return web.json_response({'version': 1, 'sessions': []})
            bodies = {'large': ' ' * 262145, 'duplicate': '{"version":1,"version":1,"sessions":[]}',
                      'nan': '{"version":1,"sessions":[],"x":NaN}', 'malformed': '{',
                      'missing': '{"version":1}', 'valid': '{"version":1,"sessions":[]}'}
            return web.Response(text=bodies[mode[0]], content_type='application/json')
        async def sink(request):
            reached.append(1)
            return web.json_response({})
        app = web.Application()
        app.router.add_get(bridge.USAGE_ROUTE, respond)
        app.router.add_get('/sink', sink)
        runner, base = await serve(app)
        class Credentials:
            expires_at = float('inf')
            async def token(self): return 'fixture-access'
        try:
            async with auth.new_client() as client:
                sample = bridge.Sample(b'k' * 32)
                gateway = bridge.UsageGateway(base, Credentials(), sample, client)
                for value in ('large', 'duplicate', 'nan', 'malformed', 'missing', 'redirect'):
                    mode[0] = value
                    sample.update([])
                    self.assertFalse(await gateway.poll_once(), value)
                    self.assertIsNone(sample.body, value)
                mode[0] = 'slow'
                sample.update([])
                self.assertFalse(await gateway.poll_once())
                self.assertIsNone(sample.body)
                mode[0] = 'valid'
                self.assertTrue(await gateway.poll_once())
                self.assertEqual(sample.body, b'WLS4\0\0\0\0')
            self.assertEqual(reached, [])
        finally:
            await runner.cleanup()

    async def test_expired_upstream_credential_and_revoked_board_fail_closed(self):
        sample = bridge.Sample(b'k' * 32)
        sample.update([])
        sample.auth_expires = time.time() - 1
        authorization = authorizer()
        runner, base = await serve(bridge.make_app(sample, authorization))
        try:
            async with auth.new_client() as client:
                headers = {'Authorization': 'Bearer ' + TOKEN}
                self.assertEqual((await client.get(base + '/v1/live', headers=headers)).status, 503)
                sample.auth_expires = time.time() + 60
                self.assertEqual((await client.get(base + '/v1/live', headers=headers)).status, 200)
                board = authorization.registry.match(bytes.fromhex(TOKEN))
                authorization.registry.remove(board['id'])
                self.assertEqual((await client.get(base + '/v1/live', headers=headers)).status, 401)
        finally:
            await runner.cleanup()


class ParserTests(unittest.TestCase):
    def test_strip_private_fields_and_reject_duplicate_nan(self):
        self.assertEqual(auth.parse_json('{"id":"a","title":"secret","preview":"secret","status":"working"}'),
                         {'id': 'a', 'status': 'working'})
        for raw in ('{"id":1,"id":2}', '{"a":NaN}', '{"a":Infinity}'):
            with self.assertRaises(ValueError):
                auth.parse_json(raw)
        for base in ('http://localhost:9119', 'http://8.8.8.8:9119', 'http://127.0.0.1:9119/evil',
                     'http://user@127.0.0.1:9119', 'https://127.0.0.1:9119'):
            with self.assertRaises(ValueError):
                auth.local_base(base)
