"""Current bots: provider metadata, WBT1 integrity, real HTTP phone policy and quota availability."""
import json
import pathlib
import struct
import tempfile
import time
import unittest
import zlib
from datetime import datetime, timedelta, timezone

from aiohttp.test_utils import TestClient, TestServer

from waveshare_bridge import bots
from waveshare_bridge import live_bridge as bridge
from tests.test_transport import ROOT
from tests.support import TOKEN, live_sample, authorizer

NOW = datetime(2026, 1, 1, 12, 0, 0, tzinfo=timezone.utc)
AUTH = {'Authorization': 'Bearer ' + TOKEN}


def iso(delta_s):
    return (NOW + timedelta(seconds=delta_s)).isoformat()


def window(used, reset_s):
    return {'label': 'Current week', 'used_percent': used, 'reset_at': None if reset_s is None else iso(reset_s)}


def ok(*windows, details=()):
    return {'label': 'x', 'plan': None, 'unavailable_reason': None, 'windows': list(windows),
            'details': list(details)}


PROFILE_YAML = {
    'helper': 'model:\n  default: qwen/qwen3-235b-a22b-2507\n  provider: openrouter\n  base_url: https://example\n'
              'display:\n  skin: default\nauxiliary:\n  vision:\n    provider: anthropic\n',
    'atlas': '# comment\nmodel:\n  default: claude-sonnet-5\n  provider: "anthropic"  # inline\ntoolsets: []\n',
    'coding': 'model:\n  default: claude-opus-4-8\n  provider: anthropic\n  routing:\n    provider: openai-codex\n'
              'fallback:\n  provider: openai-codex\n',
}


class Fixture:
    def __init__(self, providers=None, fetched_delta=-30, profiles=PROFILE_YAML):
        self.dir = tempfile.TemporaryDirectory(dir=ROOT)
        root = pathlib.Path(self.dir.name)
        self.profiles = root / 'profiles'
        for name, text in profiles.items():
            (self.profiles / name).mkdir(parents=True)
            (self.profiles / name / 'config.yaml').write_text(text)
        self.cache = root / 'quota_cache.json'
        self.write(providers if providers is not None else {
            'openrouter': ok(window(40, None), details=['Credits balance: $1.56']),
            'anthropic': ok(window(30, 3600), window(62, 86400))}, fetched_delta)

    def write(self, providers, fetched_delta=-30):
        self.cache.write_text(json.dumps({'fetched_at': iso(fetched_delta), 'providers': providers}))

    def source(self, probe=None, **kw):
        return bots.BotSource(self.cache, self.profiles, probe=probe, wall=lambda: NOW.timestamp(),
                              log=lambda *_: None, **kw)

    def cleanup(self):
        self.dir.cleanup()


class FakeCapability:
    """Plugin capability fixture; records authenticated capability probes."""

    def __init__(self, bots_status=200, bots_list=('helper', 'atlas', 'coding')):
        self.bots_status, self.bots_list = bots_status, list(bots_list)
        self.probes = 0
        self.html = False

    async def bots(self):
        self.probes += 1
        if self.html:
            return 200, None  # non-JSON capability is unusable
        if self.bots_status != 200:
            return self.bots_status, None
        return 200, {'version': 1, 'bots': self.bots_list, 'default': 'helper'}





class ProviderMapTests(unittest.TestCase):
    def setUp(self):
        self.fx = Fixture()

    def tearDown(self):
        self.fx.cleanup()

    def test_model_provider_from_each_profile_block_only(self):
        got = {b: bots.profile_provider(self.fx.profiles, b) for b in ('helper', 'atlas', 'coding')}
        self.assertEqual(got, {'helper': 'openrouter', 'atlas': 'anthropic', 'coding': 'anthropic'})

    def test_missing_malformed_symlink_and_traversal_are_none(self):
        self.assertIsNone(bots.profile_provider(self.fx.profiles, 'nobody'))
        self.assertIsNone(bots.profile_provider(self.fx.profiles, '../profiles/atlas'))
        self.assertIsNone(bots.profile_provider(self.fx.profiles, 'default'))
        (self.fx.profiles / 'flat').mkdir()
        (self.fx.profiles / 'flat' / 'config.yaml').write_text('model: gpt-5\nprovider: openrouter\n')
        self.assertIsNone(bots.profile_provider(self.fx.profiles, 'flat'))
        (self.fx.profiles / 'link').mkdir()
        (self.fx.profiles / 'link' / 'config.yaml').symlink_to(self.fx.profiles / 'atlas' / 'config.yaml')
        self.assertIsNone(bots.profile_provider(self.fx.profiles, 'link'))

    def test_override_map_wins(self):
        source = self.fx.source(providers={'helper': 'anthropic'})
        self.assertEqual(source.provider('helper'), 'anthropic')
        self.assertEqual(source.provider('atlas'), 'anthropic')

    def test_display_names_and_labels(self):
        snap = self.fx.source(names={'atlas': 'Atlas AI'}).snapshot()
        self.assertEqual([(b['id'], b['name'], b['provider_label']) for b in snap['bots']],
                         [('helper', 'Helper', 'OpenRouter'), ('atlas', 'Atlas AI', 'Claude'),
                          ('coding', 'Coding', 'Claude')])


class AvailabilityTests(unittest.IsolatedAsyncioTestCase):
    now = NOW.timestamp()

    def test_exhausted_window_with_future_reset_blocks_with_reset(self):
        self.assertEqual(bots.assess(ok(window(30, 3600), window(100, 7800)), self.now), (False, 'exhausted', 7800))

    def test_reset_clock_is_not_evidence_of_available_quota(self):
        self.assertEqual(bots.assess(ok(window(100, -60)), self.now), (False, 'exhausted', None))
        self.assertEqual(bots.assess(ok(window(2, 3600)), self.now), (True, 'none', None))

    def test_exhausted_without_reset_blocks_unknown_reset(self):
        self.assertEqual(bots.assess(ok(window(100.0, None)), self.now), (False, 'exhausted', None))

    def test_auth_reasons_are_signin(self):
        for reason in ('no-credentials', 'not-logged-in'):
            rec = {'unavailable_reason': reason, 'windows': []}
            self.assertEqual(bots.assess(rec, self.now), (False, 'signin', None))

    def test_transient_or_unknown_data_blocks_as_upstream(self):
        for rec in (None, {}, {'unavailable_reason': 'timeout'}, ok('bad-window'), ok({'used_percent': 'x'}),
                    ok({'used_percent': float('nan')})):
            self.assertEqual(bots.assess(rec, self.now), (False, 'upstream', None))

    def test_openrouter_credit_balance(self):
        self.assertFalse(bots.assess(ok(window(10, None), details=['Credits balance: $0.00']), self.now)[0])
        self.assertFalse(bots.assess(ok(details=['Credits balance: -$2.10']), self.now)[0])
        self.assertTrue(bots.assess(ok(window(89.8, None), details=['Credits balance: $1.56']), self.now)[0])
        self.assertTrue(bots.assess(ok(details=['Credits balance: unknown']), self.now)[0])

    def test_snapshot_marks_stale_cache_unreachable_and_stale(self):
        fx = Fixture({'anthropic': ok(window(100, 3600))}, fetched_delta=-3600)
        try:
            snap = fx.source().snapshot()
            self.assertTrue(snap['flags'] & bots.FLAG_QUOTA_STALE)
            self.assertTrue(all(not b['available'] and b['stale'] for b in snap['bots']))
            self.assertEqual([b['reason'] for b in snap['bots']], ['upstream','upstream','upstream'])
        finally:
            fx.cleanup()

    def test_missing_cache_is_unreachable_stale(self):
        fx = Fixture()
        try:
            fx.cache.unlink()
            snap = fx.source().snapshot()
            self.assertTrue(all(not b['available'] and b['stale'] and b['reason'] == 'upstream'
                                for b in snap['bots']))
        finally:
            fx.cleanup()

    async def test_per_bot_from_shared_provider(self):
        fx = Fixture({'openrouter': ok(window(40, None), details=['Credits balance: $3']),
                      'anthropic': ok(window(100, 7800))})
        try:
            probe = bots.PluginProbe(FakeCapability(), log=lambda *_: None)
            await probe.refresh()
            snap = {b['id']: b for b in fx.source(probe).snapshot()['bots']}
            self.assertTrue(snap['helper']['available'])
            for bot in ('atlas', 'coding'):
                self.assertEqual((snap[bot]['available'], snap[bot]['reason'], snap[bot]['reset_in_s']),
                                 (False, 'exhausted', 7800))
        finally:
            fx.cleanup()


class FrameTests(unittest.TestCase):
    def test_round_trip_fixed_size_crc(self):
        rows = [{'id': 'helper', 'name': 'Helper', 'provider_label': 'OpenRouter', 'available': True,
                 'reason': 'none', 'reset_in_s': None, 'stale': False},
                {'id': 'atlas', 'name': 'Atlas', 'provider_label': 'Claude', 'available': False,
                 'reason': 'exhausted', 'reset_in_s': 7800, 'stale': False},
                {'id': 'coding', 'name': 'Coding', 'provider_label': 'Claude', 'available': True,
                 'reason': 'plugin_update', 'reset_in_s': None, 'stale': True}]
        frame = bots.encode_frame(rows, bots.FLAG_QUOTA_STALE)
        self.assertEqual(len(frame), bots.FRAME_SIZE)
        self.assertEqual(bots.FRAME_SIZE, 8 + 3 * 44 + 4)
        self.assertEqual(frame[:4], b'WBT1')
        self.assertEqual(bots.decode_frame(frame), {'flags': bots.FLAG_QUOTA_STALE, 'bots': rows})

    def test_corruption_rejected(self):
        frame = bytearray(bots.encode_frame([], 0))
        frame[10] ^= 1
        with self.assertRaises(ValueError):
            bots.decode_frame(bytes(frame))
        bad = bytearray(bots.encode_frame([], 0))
        bad[5] = 4  # count > 3
        bad[-4:] = struct.pack('<I', zlib.crc32(bytes(bad[:-4])))
        with self.assertRaises(ValueError):
            bots.decode_frame(bytes(bad))

    def test_golden_frame_for_firmware(self):
        rows = [{'id': 'helper', 'name': 'Helper', 'provider_label': 'OpenRouter', 'available': True,
                 'reason': 'none', 'reset_in_s': None, 'stale': False},
                {'id': 'atlas', 'name': 'Atlas', 'provider_label': 'Claude', 'available': False,
                 'reason': 'exhausted', 'reset_in_s': 7800, 'stale': False}]
        golden = (ROOT / 'fixtures' / 'bots_frame.hex').read_text().split()
        self.assertEqual(bots.encode_frame(rows, bots.FLAG_PLUGIN_BOTS).hex(), ''.join(golden))


class ProbeTests(unittest.IsolatedAsyncioTestCase):
    async def test_supported_missing_and_unknown(self):
        relay = FakeCapability()
        probe = bots.PluginProbe(relay, log=lambda *_: None)
        self.assertFalse(probe.routable('helper') or probe.routable('atlas'))  # unknown: fail closed
        await probe.refresh()
        self.assertTrue(probe.routable('atlas') and probe.routable('coding'))
        relay.bots_status = 404
        await probe.refresh()
        self.assertIsNone(probe.supported)
        self.assertFalse(probe.routable('helper') or probe.routable('atlas'))
        relay.bots_status = 503
        await probe.refresh()
        self.assertIsNone(probe.supported)
        down = FakeCapability(bots_status=503)
        fresh = bots.PluginProbe(down, log=lambda *_: None)
        await fresh.refresh()
        self.assertIsNone(fresh.supported)

    async def test_outage_closes_all_bots_and_recovery_reopens(self):
        fx = Fixture()
        try:
            relay = FakeCapability()
            probe = bots.PluginProbe(relay, log=lambda *_: None)
            await probe.refresh()
            source = fx.source(probe)
            self.assertTrue(all(b['available'] for b in source.snapshot()['bots']))
            relay.bots_status = 503
            await probe.refresh()
            down = source.snapshot()['bots']
            self.assertTrue(all(not b['available'] and b['reason'] == 'upstream' for b in down))
            self.assertEqual(source.snapshot()['bots'][0]['reason'], 'upstream')
            relay.bots_status = 200
            await probe.refresh()
            self.assertTrue(all(b['available'] for b in source.snapshot()['bots']))
            self.assertTrue(source.snapshot()['bots'][0]['available'])
        finally:
            fx.cleanup()

    async def test_expired_capability_closes_while_refresh_is_pending(self):
        now = [10.0]
        probe = bots.PluginProbe(FakeCapability(), mono=lambda: now[0], ttl=60, log=lambda *_: None)
        await probe.refresh()
        self.assertTrue(probe.routable('helper'))
        now[0] = 71.0
        probe.maybe_refresh()
        self.assertIsNone(probe.supported)
        self.assertFalse(probe.routable('helper'))
        await probe.close()

    async def test_missing_route_is_unknown(self):
        probe = bots.PluginProbe(FakeCapability(bots_status=503), log=lambda *_: None)
        await probe.refresh()
        self.assertIsNone(probe.supported)

    async def test_non_json_capability_is_unknown(self):
        relay = FakeCapability()
        relay.html = True
        probe = bots.PluginProbe(relay, log=lambda *_: None)
        await probe.refresh()
        self.assertIsNone(probe.supported)

    async def test_missing_route_disables_every_bot_with_upstream(self):
        fx = Fixture()
        try:
            probe = bots.PluginProbe(FakeCapability(bots_status=404), log=lambda *_: None)
            await probe.refresh()
            snap = {b['id']: b for b in fx.source(probe).snapshot()['bots']}
            self.assertEqual(snap['helper']['reason'], 'upstream')
            for bot in ('atlas', 'coding'):
                self.assertEqual((snap[bot]['available'], snap[bot]['reason']), (False, 'upstream'))
        finally:
            fx.cleanup()


class EndpointTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.fx = Fixture()
        self.relay = FakeCapability()
        self.probe = bots.PluginProbe(self.relay, log=lambda *_: None)
        await self.probe.refresh()
        self.source = self.fx.source(self.probe)
        from waveshare_bridge import phone_pair
        auth = authorizer()
        board = auth.registry.match(bytes.fromhex(TOKEN))
        auth.registry.set_phone(board['id'], 'sam', 'Sam', groups=['admins'])
        self.phone = phone_pair.PhonePairing.from_specs(auth.registry, None, [
            {'provider': 'hermes', 'issuer_base': 'https://auth.example.com', 'groups': ['admins']}])
        self.client = TestClient(TestServer(bridge.make_app(live_sample(), auth,
                                                            bots=self.source, phone=self.phone)))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        self.fx.cleanup()

    async def bot(self, name):
        response = await self.client.get('/v1/bots', headers=AUTH)
        self.assertEqual(response.status, 200)
        return next(b for b in bots.decode_frame(await response.read())['bots'] if b['id'] == name)

    async def test_bots_requires_token_and_get(self):
        self.assertEqual((await self.client.get('/v1/bots')).status, 401)
        self.assertEqual((await self.client.get('/v1/bots', headers={'Authorization': 'Bearer ' + 'd' * 64})).status, 401)
        self.assertEqual((await self.client.post('/v1/bots', headers=AUTH)).status, 405)
        self.assertEqual((await self.client.get('/v1/bots?x=1', headers=AUTH)).status, 404)

    async def test_first_open_after_idle_refreshes_expired_health_before_reply(self):
        self.probe.checked -= self.probe.ttl + 1
        response = await self.client.get('/v1/bots', headers=AUTH)
        decoded = bots.decode_frame(await response.read())
        self.assertTrue(all(b['available'] and b['reason'] == 'none' for b in decoded['bots']))

    async def test_bots_frame(self):
        response = await self.client.get('/v1/bots', headers=AUTH)
        self.assertEqual(response.status, 200)
        self.assertEqual(response.content_type, 'application/octet-stream')
        self.assertEqual(response.headers['Cache-Control'], 'no-store')
        decoded = bots.decode_frame(await response.read())
        self.assertTrue(decoded['flags'] & bots.FLAG_PLUGIN_BOTS)
        self.assertEqual([(b['id'], b['provider_label'], b['available'], b['reason']) for b in decoded['bots']],
                         [('helper', 'OpenRouter', True, 'none'), ('atlas', 'Claude', True, 'none'),
                          ('coding', 'Claude', True, 'none')])


    async def test_exhaustion_and_recovery_change_only_affected_bots(self):
        for reset in (7800, -60):
            self.fx.write({'openrouter': ok(window(40, None)), 'anthropic': ok(window(100, reset))})
            for name in ('atlas', 'coding'):
                row = await self.bot(name)
                self.assertEqual((row['available'], row['reason']), (False, 'exhausted'))
            self.assertTrue((await self.bot('helper'))['available'])
        self.fx.write({'openrouter': ok(window(40, None)), 'anthropic': ok(window(2, 3600))})
        self.assertTrue((await self.bot('atlas'))['available'])

    async def test_usage_signin_and_stale_telemetry_do_not_disable_routable_bots(self):
        for providers, delta in [({'openrouter': {'unavailable_reason': 'no-credentials', 'windows': []}}, -30),
                                 ({'openrouter': ok(), 'anthropic': ok()}, -10000)]:
            self.fx.write(providers, fetched_delta=delta)
            self.assertTrue((await self.bot('helper'))['available'])

    async def test_missing_capability_is_upstream_for_every_bot_and_recovers(self):
        for status in (404, 503):
            self.relay.bots_status = status
            await self.probe.refresh()
            for name in ('helper', 'atlas', 'coding'):
                row = await self.bot(name)
                self.assertEqual((row['available'], row['reason']), (False, 'upstream'))
        self.relay.bots_status = 200
        await self.probe.refresh()
        self.assertTrue((await self.bot('coding'))['available'])


class ConfigTests(unittest.TestCase):
    def test_default_profiles_dir_and_bot_validation(self):
        from waveshare_bridge import config
        self.assertEqual(config.default_profiles_dir({}), pathlib.Path.home() / '.hermes' / 'profiles')
        self.assertEqual(config.default_profiles_dir({'HERMES_HOME': '/tmp/hh'}), pathlib.Path('/tmp/hh/profiles'))
        for value in (['default'], ['a', 'a'], ['x', 'y', 'z', 'w'], ['../x']):
            with self.assertRaises(ValueError):
                bots.BotSource(None, '/tmp', bots=value)


if __name__ == '__main__':
    unittest.main()
