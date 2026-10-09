"""Exercise only the actual scoped HTTP /bots client, not relay command fixtures."""
import unittest
from aiohttp import web
from waveshare_bridge import bots, common, authelia_client
from tests.test_authelia_client import FakeIdP, client_config, ACCESS
from tests.test_transport import serve


class CapabilityTransportTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.idp = FakeIdP()
        self.calls = []
        self.mode = 'ok'
        self.rejections = 0
        self.sinks = 0
        app = web.Application()
        app.router.add_post('/api/oidc/token', self.idp.token)
        app.router.add_get('/api/plugins/waveshare-sessions/bots', self.capability)
        app.router.add_get('/sink', self.sink)
        self.runner, self.base = await serve(app)
        self.http = common.new_client()
        self.credentials = authelia_client.ClientCredentials(client_config(self.base + '/api/oidc/token'), self.http)
        self.source = bots.CapabilityClient(self.base, self.credentials, self.http)
        self.probe = bots.PluginProbe(self.source, log=lambda *_: None)

    async def asyncTearDown(self):
        await self.probe.close()
        await self.http.close()
        await self.runner.cleanup()

    async def sink(self, request):
        self.sinks += 1
        return web.json_response({})

    async def capability(self, request):
        self.calls.append((request.path, request.headers.get('Authorization')))
        if self.rejections:
            self.rejections -= 1
            return web.Response(status=401)
        if self.mode == 'missing': return web.Response(status=404)
        if self.mode == 'redirect': return web.Response(status=302, headers={'Location': '/sink'})
        if self.mode == 'large': return web.Response(text=' ' * 65537, content_type='application/json')
        if self.mode == 'duplicate': return web.Response(text='{"version":1,"bots":[],"bots":[]}', content_type='application/json')
        if self.mode == 'version': return web.json_response({'version': 2, 'bots': ['helper']})
        if self.mode == 'invalid': return web.json_response({'version': 1, 'bots': ['helper', '../default']})
        return web.json_response({'version': 1, 'bots': ['helper', 'atlas', 'coding'], 'default': 'helper'})

    async def test_authenticated_current_capability_retries_auth_once_and_only_calls_bots(self):
        self.rejections = 1
        await self.probe.refresh()
        self.assertTrue(self.probe.routable('coding'))
        self.assertEqual(self.idp.issued, 2)
        self.assertEqual(len(self.calls), 2)
        self.assertTrue(all(path.endswith('/bots') and token.startswith('Bearer ' + ACCESS) for path, token in self.calls))
        self.assertEqual(self.sinks, 0)

    async def test_missing_redirect_oversize_duplicate_clear_all_capabilities(self):
        for mode in ('missing', 'redirect', 'large', 'duplicate'):
            self.mode = 'ok'
            await self.probe.refresh()
            self.assertTrue(self.probe.routable('helper'))
            self.mode = mode
            await self.probe.refresh()
            self.assertIsNone(self.probe.supported, mode)
            self.assertFalse(self.probe.routable('helper'), mode)
        self.assertEqual(self.sinks, 0)

    async def test_wrong_version_or_malformed_bot_list_never_authorizes(self):
        for mode in ('version', 'invalid'):
            with self.subTest(mode=mode):
                self.mode = mode
                await self.probe.refresh()
                self.assertIsNone(self.probe.supported, mode)
                self.assertFalse(self.probe.routable('helper'), mode)
