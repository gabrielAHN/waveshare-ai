import unittest
from waveshare_bridge import bots
from tests.test_bots import Fixture, FakeCapability, ok, window

class OptionalQuotaTests(unittest.IsolatedAsyncioTestCase):
 async def test_fresh_error_cache_requests_bounded_official_refresh(self):
  class Refresh:
   def __init__(self):self.calls=0
   def _maybe_refresh(self):self.calls+=1
  refresh=Refresh()
  fx=Fixture({'anthropic':{'unavailable_reason':'fetcher-unavailable','windows':[]}});self.addCleanup(fx.cleanup)
  probe=bots.PluginProbe(FakeCapability(),log=lambda *_:None);await probe.refresh()
  src=fx.source(probe=probe,quota_source=refresh)
  src.blocks['anthropic']='exhausted'
  self.assertEqual(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['reason'], 'exhausted')
  self.assertGreater(refresh.calls,0)
 async def test_stale_record_cannot_replace_newer_proven_quota_recovery(self):
  fx=Fixture({'anthropic':ok(window(100,100))});self.addCleanup(fx.cleanup)
  probe=bots.PluginProbe(FakeCapability(),log=lambda *_:None);await probe.refresh()
  src=fx.source(probe=probe)
  self.assertEqual(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['reason'], 'exhausted')
  fx.write({'anthropic':ok(window(2,100))})
  self.assertTrue(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['available'])
  fx.write({'anthropic':ok(window(100,-10))},fetched_delta=-3600)
  self.assertTrue(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['available'])
  self.assertNotIn('anthropic',src.blocks)
 async def test_reachable_idle_bots_do_not_require_optional_quota_metadata(self):
  fx=Fixture({}); self.addCleanup(fx.cleanup)
  probe=bots.PluginProbe(FakeCapability(),log=lambda *_:None); await probe.refresh()
  src=fx.source(probe=probe)
  self.assertTrue(all(b['available'] and b['reason']=='none' for b in src.snapshot()['bots']))
  self.assertTrue(next(b for b in src.snapshot()['bots'] if b['id'] == 'helper')['available'])
 async def test_stale_exhausted_without_proven_latch_does_not_infer_a_block(self):
  fx=Fixture({'anthropic':ok(window(100,-10))},fetched_delta=-3600);self.addCleanup(fx.cleanup)
  probe=bots.PluginProbe(FakeCapability(),log=lambda *_:None);await probe.refresh()
  src=fx.source(probe=probe); rows=src.snapshot()['bots']
  self.assertTrue(all(b['available'] for b in rows))
  self.assertTrue(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['available'])
 async def test_proven_exhaustion_survives_errors_and_bridge_restart(self):
  fx=Fixture({'anthropic':ok(window(100,10))});self.addCleanup(fx.cleanup)
  probe=bots.PluginProbe(FakeCapability(),log=lambda *_:None);await probe.refresh()
  path=fx.cache.parent/'quota-blocks.json'
  src=fx.source(probe=probe,gate_path=path)
  self.assertEqual(src.snapshot()['bots'][1]['reason'],'exhausted')
  fx.write({'anthropic':{'unavailable_reason':'timeout','windows':[]}})
  self.assertEqual(src.snapshot()['bots'][1]['reason'],'exhausted')
  src=fx.source(probe=probe,gate_path=path)
  self.assertEqual(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['reason'], 'exhausted')
  fx.write({'anthropic':ok()})  # no usable quota telemetry is not proof of replenishment
  self.assertEqual(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['reason'], 'exhausted')
  fx.write({'anthropic':ok(window(2,100))})
  self.assertTrue(next(b for b in src.snapshot()['bots'] if b['id'] == 'atlas')['available'])
 async def test_refresh_does_not_invalidate_still_fresh_capability(self):
  clock=[0.0];probe=bots.PluginProbe(FakeCapability(),mono=lambda:clock[0],ttl=60,log=lambda *_:None)
  await probe.refresh();clock[0]=55;probe.maybe_refresh()
  self.assertTrue(probe.routable('helper'));self.assertIsNotNone(probe.task)
  await probe.close();clock[0]=61;self.assertFalse(probe.routable('helper'))
