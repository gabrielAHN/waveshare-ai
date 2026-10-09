"""A provider's usage check saying "sign in" is not Hermes sign-in.

Hermes routes every bot through its own authenticated gateway; the board's sign-in is the phone QR.
The official quota cache's auth reasons ('not-logged-in', expired tokens, ...) only say the usage
check could not read that provider's quota. They must never grey a bot out with "Sign in on the
Mac" (user, 2026-10-07: "they should since they are all connected via hermes auth"), never latch
in quota-blocks.json, and never disable a routable bot. Real quota exhaustion still blocks.
"""
import json
import unittest

from waveshare_bridge import bots
from tests.test_bots import Fixture, FakeCapability, ok, window

AUTH_RECORD = {'unavailable_reason': 'not-logged-in', 'windows': []}


class UsageSigninTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.probe = bots.PluginProbe(FakeCapability(), log=lambda *_: None)
        await self.probe.refresh()

    def fixture(self, providers, **kw):
        fx = Fixture(providers, **kw)
        self.addCleanup(fx.cleanup)
        return fx

    async def test_usage_signin_does_not_grey_out_a_routable_bot(self):
        fx = self.fixture({'openrouter': ok(window(10, 3600)), 'anthropic': AUTH_RECORD})
        src = fx.source(probe=self.probe)
        rows = src.snapshot()['bots']
        self.assertTrue(all(b['available'] and b['reason'] == 'none' for b in rows), rows)
        self.assertTrue(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['available'])
        self.assertTrue(next(b for b in src.snapshot()['bots'] if b['id'] == 'helper')['available'])

    async def test_usage_signin_never_latches(self):
        fx = self.fixture({'anthropic': AUTH_RECORD})
        path = fx.cache.parent / 'quota-blocks.json'
        src = fx.source(probe=self.probe, gate_path=path)
        src.snapshot()
        self.assertNotIn('anthropic', src.blocks)

    async def test_usage_signin_on_an_unroutable_bot_is_upstream_not_signin(self):
        fx = self.fixture({'anthropic': AUTH_RECORD})
        relay = FakeCapability()
        relay.bots_list = ['helper']
        probe = bots.PluginProbe(relay, log=lambda *_: None)
        await probe.refresh()
        rows = {b['id']: b for b in fx.source(probe=probe).snapshot()['bots']}
        self.assertFalse(rows['atlas']['available'])
        self.assertNotEqual(rows['atlas']['reason'], 'signin')

    async def test_real_exhaustion_still_blocks(self):
        fx = self.fixture({'anthropic': ok(window(100, 7800))})
        src = fx.source(probe=self.probe)
        self.assertEqual(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['reason'], 'exhausted')


if __name__ == '__main__':
    unittest.main()
