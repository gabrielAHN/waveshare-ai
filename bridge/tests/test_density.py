"""Token-density level: status parsing, tracker math, WLS4 wire and the
read-only usage sweep against an ephemeral gateway fixture."""
import asyncio
import pathlib
import struct
import tempfile
import time
import unittest
PKG = __import__('pathlib').Path(__file__).resolve().parent.parent / 'waveshare_bridge'

import aiohttp
from aiohttp import web
from waveshare_bridge import live_bridge as bridge
from waveshare_bridge import common as auth
from tests.test_transport import ROOT, serve

KEY = b'k' * 32




class StatusParseTests(unittest.TestCase):

    def test_thresholds(self):
        self.assertEqual(bridge.parse_thresholds('20000,100000,200000,350000,600000'), bridge.DEFAULT_THRESHOLDS)
        for bad in ('1,2,3,4', '1,2,3,4,5,6', '5,4,3,2,1', '0,1,2,3,4', '1,1,2,3,4', 'a,b,c,d,e'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                bridge.parse_thresholds(bad)


class TrackerTests(unittest.TestCase):
    def rows(self, *working, idle=()):
        return [{'id': s, 'status': 'working'} for s in working] + [{'id': s, 'status': 'idle'} for s in idle]

    def feed(self, tracker, per_minute, seconds, step=5.0, start=0.0, sid='a', total=0):
        t = start
        tracker.roster(self.rows(sid))
        while t < start + seconds:
            tracker.observe({sid: total}, t)
            t += step
            total += int(per_minute * step / 60)
        return t, total

    def test_level_mapping_all_six(self):
        for rate, expected in [(0, 0), (10000, 0), (20000, 1), (99999, 1), (100000, 2), (199999, 2),
                               (200000, 3), (349999, 3), (350000, 4), (599999, 4), (600000, 5), (10 ** 7, 5)]:
            tracker = bridge.DensityTracker()
            self.feed(tracker, rate, 300, step=60.0)  # exact integer deltas per minute
            with self.subTest(rate=rate):
                self.assertAlmostEqual(tracker.ema, rate, delta=1e-6 * max(rate, 1))
                self.assertEqual(tracker.wire(tracker.measured_at)[0], expected)

    def test_one_call_is_a_range_not_a_spike(self):
        """A real Hermes session: one ~50k-token model call every ~12 s (state.db: ~50k tokens/call,
        5 s sweeps). The level sits mid-range and stays there between calls."""
        tracker = bridge.DensityTracker()
        tracker.roster(self.rows('a'))
        t, total, levels = 0.0, 0, []
        tracker.observe({'a': total}, t)
        for i in range(60):
            t += 5
            if i % 12 in (0, 5, 10):  # three calls a minute
                total += 50000
            tracker.observe({'a': total}, t)
            if t >= 60:
                levels.append(tracker.wire(t)[0])
        self.assertEqual(set(levels), {2})  # 150k tok/min, steady between calls
        # A second session at the same pace moves up the range; a burst of both reaches the top.
        tracker.roster(self.rows('a', 'b'))
        tracker.observe({'a': total, 'b': 0}, t + 1)
        for i in range(24):
            t += 5
            if i % 4 == 0:
                total += 50000
            tracker.observe({'a': total, 'b': 50000 * (i + 1)}, t)
        self.assertEqual(tracker.wire(t)[0], 5)
        # All calls finish: the level walks down through the range instead of dropping at once.
        down = []
        for _ in range(16):
            t += 5
            tracker.observe({'a': total, 'b': 50000 * 24}, t)
            down.append(tracker.wire(t)[0])
        self.assertEqual(down[-1], 0)
        self.assertGreater(len(set(down)), 2)

    def test_no_working_is_zero_and_unmeasured_warmup(self):
        tracker = bridge.DensityTracker()
        tracker.roster([])
        self.assertEqual(tracker.wire(), (0, 0))
        tracker.roster(self.rows('a', 'b', 'c'))
        self.assertEqual(tracker.wire(), (0, 0))  # first sweep pending: calm, not count
        tracker.observe({'a': 10, 'b': 10, 'c': 10}, 1.0)  # baselines only
        self.assertEqual(tracker.wire(1.0), (0, 0))
        tracker.roster(self.rows(idle=('a',)))
        self.assertEqual(tracker.wire(), (0, 0))

    def test_negative_delta_and_disappear_reappear(self):
        tracker = bridge.DensityTracker()
        tracker.roster(self.rows('a'))
        tracker.observe({'a': 1_000_000}, 0.0)
        tracker.observe({'a': 10}, 5.0)  # compression/reset: counts as 0, re-baselines
        self.assertEqual(tracker.ema, 0.0)
        tracker.observe({'a': 10 + 700_000}, 10.0)  # 700k in the last minute
        self.assertEqual(tracker.level, 5)
        tracker.roster(self.rows('b'))  # 'a' gone -> its baseline dropped
        self.assertNotIn('a', tracker.baselines)
        self.assertNotIn('a', tracker.history)
        tracker.roster(self.rows('a'))
        tracker.observe({'a': 9_999_999}, 15.0)  # reappears: baseline only, no spike
        self.assertEqual(tracker.baselines['a'], (9_999_999, 15.0))
        self.assertEqual(tracker.ema, 0.0)

    def test_hysteresis_no_flicker(self):
        tracker = bridge.DensityTracker()
        t, total = self.feed(tracker, 210000, 200)  # just above the L3 threshold
        self.assertEqual(tracker.level, 3)
        levels = set()
        for i in range(60):  # oscillate +-15% around the threshold
            rate = 200000 * (0.85 if i % 2 else 1.15)
            total += int(rate * 5 / 60)
            t += 5
            tracker.observe({'a': total}, t)
            levels.add(tracker.level)
        self.assertEqual(levels, {3})  # hysteresis holds, no flicker
        for _ in range(40):  # sustained drop well below 0.8*threshold
            total += int(120000 * 5 / 60)
            t += 5
            tracker.observe({'a': total}, t)
        self.assertEqual(tracker.level, 2)

    def test_failures_report_unavailable_then_recover(self):
        tracker = bridge.DensityTracker(stale_seconds=30)
        tracker.roster(self.rows('a', 'b'))
        tracker.fail()
        self.assertEqual(tracker.wire(0), (0, bridge.FLAG_DEGRADED))
        tracker.observe({'a': 0, 'b': None}, 1.0)
        tracker.observe({'a': 700_000, 'b': None}, 6.0)
        level, flags = tracker.wire(6.0)
        self.assertEqual(level, 5)
        self.assertEqual(flags, bridge.FLAG_MEASURED | bridge.FLAG_DEGRADED)
        self.assertEqual(tracker.wire(40.0), (0, bridge.FLAG_DEGRADED))  # measurement stale


class TurnGapTests(unittest.TestCase):
    def test_idle_gap_between_turns_keeps_the_range(self):
        """Measured live: sessions flip working -> idle between turns; dropping the rate at once made
        the tile jump 4 -> 0 every few seconds. The last minute's usage stays in the window."""
        t = bridge.DensityTracker()
        rows = [{'id': 'a', 'status': 'working'}, {'id': 'b', 'status': 'working'}]
        t.roster(rows)
        t.observe({'a': 1_000_000, 'b': 10}, 0)
        t.observe({'a': 1_300_000, 'b': 10}, 20)       # 300k in the window -> 300k tok/min
        self.assertEqual(t.wire(20)[0], 3)
        t.roster([{'id': 'a', 'status': 'idle'}, {'id': 'b', 'status': 'working'}])   # a's turn ended
        self.assertEqual(t.wire(21)[0], 3)
        t.observe({'b': 10}, 25)
        self.assertEqual(t.wire(25)[0], 3)
        t.roster(rows)                                  # a's next turn: new baseline, no lifetime delta
        t.observe({'a': 9_000_000, 'b': 10}, 30)
        self.assertEqual(t.wire(30)[0], 3)
        t.observe({'a': 9_000_000, 'b': 10}, 85)        # a minute later, nothing new: calm again
        self.assertEqual(t.wire(85)[0], 0)

    def test_outage_still_clears_everything(self):
        t = bridge.DensityTracker()
        t.roster([{'id': 'a', 'status': 'working'}])
        t.observe({'a': 0}, 0)
        t.observe({'a': 400_000}, 10)
        self.assertGreater(t.wire(10)[0], 0)
        t.clear()
        t.roster([{'id': 'a', 'status': 'working'}])
        t.observe({'a': 400_000}, 12)
        self.assertEqual(t.wire(12), (0, 0))


class ConfigWindowTests(unittest.TestCase):
    def test_service_default_window_is_a_minute(self):
        """The service builds its tracker from bridge.json defaults: a 20 s default there made one
        model call drop to level 0 twenty seconds later (seen live) despite the 60 s tracker."""
        from waveshare_bridge import config
        for data in ({'bind': '10.99.0.5', 'providers': {'hermes': {}}},):
            cfg = config.parse(data, pathlib.Path(tempfile.gettempdir()))
            self.assertEqual(cfg['ema_seconds'], 60.0)
            self.assertEqual(cfg['providers']['hermes']['sparkles']['ema_seconds'], 60.0)


class WireTests(unittest.TestCase):
    def test_wls3_header_and_validation(self):
        rows = [{'id': 'a', 'status': 'working', 'title': 'PRIVATE'}, {'id': 'b', 'status': 'idle'}]
        body = bridge.encode_sessions(rows, KEY, 4, bridge.FLAG_MEASURED)
        self.assertEqual(body[:8], b'WLS4' + struct.pack('<HBB', 2, 4, 1))
        self.assertEqual(len(body), 28)
        idle_only = bridge.encode_sessions([rows[1]], KEY, 4, bridge.FLAG_MEASURED)
        self.assertEqual(idle_only[:8], b'WLS4' + struct.pack('<HBB', 1, 0, 0))
        default = bridge.encode_sessions(rows, KEY)
        self.assertEqual(default[:8], b'WLS4' + struct.pack('<HBB', 2, 0, 0))
        self.assertNotIn(b'PRIVATE', body)
        self.assertEqual(default[8:], body[8:])
        self.assertEqual(bridge.encode_sessions([], KEY, 5, 1), b'WLS4\0\0\0\0')
        for level, flags in ((6, 0), (-1, 0), ('1', 0), (1, 4), (1, 0x80), (True, 0)):
            with self.subTest(level=level, flags=flags):
                if level is True:
                    continue
                with self.assertRaises(ValueError):
                    bridge.encode_sessions(rows, KEY, level, flags)


class UsageSweepTests(unittest.IsolatedAsyncioTestCase):
    async def test_read_only_usage_levels_and_privacy(self):
        calls = []
        state = {'total': 0, 'bad_tokens': False, 'idle': False}
        async def usage(request):
            calls.append((request.path, request.query.get('tokens')))
            rows = [{'id': 'w1', 'status': 'idle' if state['idle'] else 'working',
                     'title': 'SECRET', 'preview': 'SECRET'}]
            if request.query.get('tokens'):
                rows[0]['tokens'] = None if state['bad_tokens'] else state['total']
            return web.json_response({'version': 1, 'sessions': rows})
        app = web.Application()
        app.router.add_get(bridge.USAGE_ROUTE, usage)
        runner, base = await serve(app)
        class Credentials:
            expires_at = float('inf')
            async def token(self): return 'fixture-access'
        try:
            async with auth.new_client() as client:
                sample = bridge.Sample(KEY, bridge.DensityTracker(ema_seconds=.5))
                gateway = bridge.UsageGateway(base, Credentials(), sample, client, usage_interval=0)
                self.assertTrue(await gateway.poll_once())
                self.assertEqual(sample.body[6:8], b'\0\0')
                await asyncio.sleep(.02)
                state['total'] += 100000
                self.assertTrue(await gateway.poll_once())
                self.assertEqual(sample.body[6:8], bytes((5, bridge.FLAG_MEASURED)))
                self.assertNotIn(b'SECRET', sample.body)
                self.assertNotIn('SECRET', repr(sample.tracker.__dict__))
                state['bad_tokens'] = True
                self.assertTrue(await gateway.poll_once())
                self.assertEqual(sample.body[7], bridge.FLAG_MEASURED | bridge.FLAG_DEGRADED)
                state['idle'] = True
                self.assertTrue(await gateway.poll_once())
                self.assertEqual(sample.body[:8], b'WLS4\x01\0\0\0')
                self.assertEqual(sample.body[17], 0)
        finally:
            await runner.cleanup()
        self.assertTrue(all(path == bridge.USAGE_ROUTE and tokens == '1' for path, tokens in calls))

    async def test_usage_sampling_is_rate_limited_without_losing_roster(self):
        sample = bridge.Sample(KEY)
        gateway = bridge.UsageGateway('http://127.0.0.1:1', None, sample, None, usage_interval=5)
        class Credentials:
            expires_at = float('inf')
            async def token(self): return 'fixture-access'
        gateway.credentials = Credentials()
        observed = []
        async def get(token, with_tokens):
            observed.append(with_tokens)
            return 200, {'version': 1, 'sessions': [{'id': 'a', 'status': 'working', 'tokens': 1}]}
        gateway._get = get
        self.assertTrue(await gateway.poll_once())
        self.assertTrue(await gateway.poll_once())
        self.assertEqual(observed, [True, False])

    def test_bridge_never_sends_rpc_or_usage_queries(self):
        source = (PKG / 'live_bridge.py').read_text()
        self.assertNotIn('session.usage', source)
        self.assertNotIn('send_json', source)
        self.assertEqual(bridge.USAGE_ROUTE, '/api/plugins/waveshare-sessions/usage')


if __name__ == '__main__':
    unittest.main()
