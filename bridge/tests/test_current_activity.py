import unittest
from waveshare_bridge.live_bridge import DensityTracker, FLAG_DEGRADED, Sample

class CurrentActivityTests(unittest.TestCase):
    def test_outage_discards_rate_baselines_before_reconnect(self):
        s = Sample(b'x'*32)
        rows = [{'id':'a','status':'working'}]
        s.update(rows)
        s.tracker.observe({'a':100},1)
        s.tracker.observe({'a':100000},6)
        s.invalidate()
        s.update(rows)
        s.tracker.observe({'a':9000000},100)
        self.assertEqual(s.tracker.wire(100),(0,0))

    def test_idle_reentry_never_reuses_lifetime_baseline(self):
        t = DensityTracker()
        t.roster([{'id': 'a', 'status': 'working'}])
        t.observe({'a': 1000000}, 1)
        t.observe({'a': 1100000}, 6)
        self.assertEqual(t.wire(6)[0], 2)  # one 100k-token call: mid range (windowed), not the top
        t.roster([{'id': 'a', 'status': 'idle'}])
        t.roster([{'id': 'a', 'status': 'working'}])
        t.observe({'a': 9100000}, 11)
        # the 8M lifetime jump is never counted: only the earlier real call's range remains
        self.assertEqual(t.wire(11)[0], 2)
        t.observe({'a': 9100000}, 70)
        self.assertEqual(t.wire(70)[0], 0)  # ...until it leaves the window

    def test_unchanged_total_preserves_recent_completed_call_rate(self):
        t = DensityTracker()
        t.roster([{'id': 'a', 'status': 'working'}])
        t.observe({'a': 1000000}, 1)
        t.observe({'a': 1100000}, 6)
        t.observe({'a': 1100000}, 11)
        self.assertGreater(t.wire(11)[0], 0)
        t.observe({'a': 1100000}, 31)
        self.assertGreater(t.wire(31)[0], 0)  # still inside the 60 s window: the range holds
        t.observe({'a': 1100000}, 67)
        self.assertEqual(t.wire(67)[0], 0)    # the call left the window: back to calm

    def test_failed_or_stale_is_not_count_derived_intensity(self):
        t = DensityTracker()
        t.roster([{'id': 'a', 'status': 'working'}])
        t.fail()
        self.assertEqual(t.wire(1), (0, FLAG_DEGRADED))

    def test_backwards_clock_does_not_replace_baseline(self):
        t = DensityTracker()
        t.roster([{'id': 'a', 'status': 'working'}])
        t.observe({'a': 100}, 10)
        t.observe({'a': 1000000}, 9)
        self.assertEqual(t.baselines['a'], (100, 10))
        t.observe({'a': 100}, 15)
        self.assertEqual(t.wire(15)[0], 0)
