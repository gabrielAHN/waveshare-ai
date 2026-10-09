import unittest
from waveshare_bridge import live_bridge


class RouteContractTests(unittest.IsolatedAsyncioTestCase):
    async def test_missing_capability_never_probes_commands(self):
        from waveshare_bridge import bots
        class Capability:
            calls = 0
            async def bots(self):
                return 404, None
            async def get(self, cid):
                self.calls += 1
                return 404, {'detail': 'unknown command'}
        source = Capability()
        probe = bots.PluginProbe(source, log=lambda *_: None)
        await probe.refresh()
        self.assertEqual(source.calls, 0)
        self.assertFalse(probe.routable('helper'))

    async def test_phone_requires_explicit_provider(self):
        from aiohttp.test_utils import TestClient, TestServer
        from waveshare_bridge import phone_pair
        from tests.support import authorizer, live_sample, TOKEN
        auth = authorizer()
        phone = phone_pair.PhonePairing.from_specs(auth.registry, None, [
            {'provider': 'hermes', 'issuer_base': 'https://auth.example.com', 'groups': ['admins']}])
        async with TestClient(TestServer(live_bridge.make_app(live_sample(), auth, phone=phone))) as client:
            response = await client.get('/v1/pair/phone/status', headers={'Authorization': 'Bearer ' + TOKEN})
            self.assertEqual(response.status, 404)

    async def test_removed_routes_are_not_served(self):
        from aiohttp.test_utils import TestClient, TestServer
        from tests.support import authorizer, live_sample
        async with TestClient(TestServer(live_bridge.make_app(live_sample(), authorizer()))) as client:
            for method, path in [('POST', '/v1/voice'), ('GET', '/v1/command/' + '0' * 32),
                                 ('POST', '/v1/command/stop'), ('POST', '/v1/page/open'), ('GET', '/v1/quota')]:
                with self.subTest(path=path):
                    self.assertEqual((await client.request(method, path)).status, 404)


class CurrentWireTests(unittest.TestCase):
    def test_sdk_config_rejects_unknown_keys(self):
        import pathlib
        from waveshare_bridge import config
        value = {'port': 8768, 'profiles': {'helper': 8775}, 'transport': 'proxy'}
        with self.assertRaisesRegex(ValueError, 'gadget_sdk'):
            config.parse({'bind': '10.0.0.2', 'providers': {'hermes': {'gadget_sdk': value}}}, pathlib.Path('.'))

    def test_phone_flags_only_required_and_shared(self):
        from waveshare_bridge import phone_pair
        for flags in (2, 4, 15):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                phone_pair.encode_status('none', flags=flags)

    def test_config_rejects_voice_transport_settings(self):
        import pathlib
        from waveshare_bridge import config
        with self.assertRaisesRegex(ValueError, 'voice'):
            config.parse({'bind': '10.0.0.2', 'providers': {'hermes': {'voice': {}}}}, pathlib.Path('.'))

    def test_live_encoder_lists_all_open_sessions_in_wls4(self):
        rows = [{'id': 'a', 'status': 'working'}, {'id': 'b', 'status': 'idle'}]
        body = live_bridge.encode_sessions(rows, b'k' * 32)
        self.assertEqual(body[:6], b'WLS4\x02\0')
        self.assertEqual(len(body), 28)
