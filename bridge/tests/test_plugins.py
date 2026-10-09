"""Route groups: which ones bridge.json's providers serve (config.served_routes) and the routes each owns."""
import pathlib
import tempfile
import unittest

from aiohttp.test_utils import TestClient, TestServer

from waveshare_bridge import config, enroll
from waveshare_bridge import live_bridge as bridge
from tests.test_home_sensors import FakeHome, FakePhone
from tests.test_transport import ROOT
from tests.support import TOKEN, live_sample

AUTH = {'Authorization': 'Bearer ' + TOKEN}
CID = 'ab' * 16


class FakeFrames:
    def __init__(self, body):
        self.body, self.calls = body, 0

    def frame(self):
        self.calls += 1
        return self.body


class ConfigTests(unittest.TestCase):
    """``providers`` decides the route groups (hermes tiles sparkles/ask, home_assistant)."""

    def load(self, extra):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = pathlib.Path(tmp)
            root.chmod(0o700)
            enroll.write_private_json(root / 'bridge.json', {'bind': '10.99.0.5', 'port': 8098, **extra})
            return config.load(root)

    def test_missing_key_means_every_plugin(self):
        self.assertEqual(self.load({'providers': {'hermes': {}, 'home_assistant': {}}})['plugins'],
                         frozenset({'ai', 'home_assistant', 'sparkles'}))
        self.assertEqual(bridge.plugin_set(None), frozenset(bridge.PLUGINS))
        self.assertEqual(set(config.PLUGINS), set(bridge.PLUGINS))

    def test_subsets_and_empty(self):
        self.assertEqual(self.load({'providers': {'home_assistant': {}}})['plugins'], frozenset({'home_assistant'}))
        self.assertEqual(self.load({'providers': {'hermes': {'tiles': ['sparkles', 'ask']}}})['plugins'],
                         frozenset({'sparkles', 'ai'}))
        self.assertEqual(self.load({'providers': {'hermes': {'tiles': ['sparkles']}}})['plugins'], frozenset({'sparkles'}))
        self.assertEqual(self.load({'providers': {}})['plugins'], frozenset())

    def test_rejects_unknown_or_malformed(self):
        for bad in ('ask', ['voice'], [1], {'ask': True}, ['ask', 'Sparkles']):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.load({'providers': {'hermes': {'tiles': bad}}})
        for flat in ({}, {'plugins': ['ai']}, {'providers': {}, 'plugins': ['ai']}):   # unknown top-level keys are rejected
            with self.subTest(flat=flat), self.assertRaises(ValueError):
                self.load(flat)


class RouteTests(unittest.IsolatedAsyncioTestCase):
    async def client(self, plugins):
        from waveshare_bridge import bots, phone_pair
        from tests.support import authorizer
        auth = authorizer()
        board = auth.registry.match(bytes.fromhex(TOKEN))
        auth.registry.set_phone(board['id'], 'sam', 'Sam', groups=['admins'])
        phone = phone_pair.PhonePairing.from_specs(auth.registry, None, [
            {'provider': name, 'issuer_base': 'https://auth.example.com', 'groups': ['admins']}
            for name in ('hermes', 'home_assistant')])
        self.home = FakeHome()
        self.bots = FakeFrames(bots.encode_frame([{'id': 'helper', 'name': 'Helper', 'available': True, 'reason': 'none'}], 0))
        self.http = TestClient(TestServer(bridge.make_app(live_sample(), auth, home=self.home,
                                                        phone=phone, bots=self.bots, plugins=plugins)))
        await self.http.start_server()
        return self.http

    async def asyncTearDown(self):
        await self.http.close()

    async def statuses(self, c):
        return {name: (await c.get('/v1/' + name, headers=AUTH)).status for name in ('live', 'home', 'bots')}

    async def test_all_route_groups(self):
        self.assertEqual(await self.statuses(await self.client(None)), {'live': 200, 'home': 200, 'bots': 200})
        self.assertEqual((self.home.calls, self.bots.calls), (1, 1))

    async def test_home_assistant_only(self):
        self.assertEqual(await self.statuses(await self.client(['home_assistant'])), {'live': 404, 'home': 200, 'bots': 404})
        self.assertEqual(self.bots.calls, 0)

    async def test_sparkles_only_is_just_the_session_feed(self):
        self.assertEqual(await self.statuses(await self.client(['sparkles'])), {'live': 200, 'home': 404, 'bots': 404})
        self.assertEqual((self.home.calls, self.bots.calls), (0, 0))

    async def test_ai_only(self):
        self.assertEqual(await self.statuses(await self.client(['ai'])), {'live': 404, 'home': 404, 'bots': 200})
        self.assertEqual(self.home.calls, 0)

    async def test_disabled_routes_404_before_authentication(self):
        client = await self.client([])
        for path in ('/v1/live', '/v1/home', '/v1/bots'):
            self.assertEqual((await client.get(path)).status, 404)
        self.assertEqual((await client.get('/v1/nope', headers=AUTH)).status, 404)


if __name__ == '__main__':
    unittest.main()
