"""Bounded optional quota telemetry and single-flight fresh-interpreter refresh."""
import asyncio
import json
import os
import pathlib
import shutil
import struct
import tempfile
import unittest
import zlib
from datetime import datetime, timezone

from waveshare_bridge import quota
from waveshare_bridge import live_bridge as bridge
from tests.test_transport import ROOT, serve

FIXTURE = ROOT / 'fixtures' / 'quota_cache_sanitized.json'
FETCHED = datetime(2026, 1, 1, 12, 0, 0, tzinfo=timezone.utc).timestamp()
TOKEN = 'b' * 43
AUTH = {'Authorization': 'Bearer ' + TOKEN}


def fixture():
    return json.loads(FIXTURE.read_text())


class Clock:
    def __init__(self, wall, mono=1000.0):
        self.wall, self.mono = wall, mono

    def advance(self, seconds):
        self.wall += seconds
        self.mono += seconds


class FakeRefresher:
    """Stands in for the fresh-interpreter subprocess; records calls, never spawns anything."""
    def __init__(self, result='ok', delay=0.0, on_run=None):
        self.calls, self.result, self.delay, self.on_run = 0, result, delay, on_run

    async def __call__(self):
        self.calls += 1
        if self.delay:
            await asyncio.sleep(self.delay)
        if self.on_run:
            self.on_run()
        return self.result


class TempCache:
    def __init__(self, data=None):
        self.dir = tempfile.TemporaryDirectory(dir=ROOT)
        self.path = pathlib.Path(self.dir.name) / 'quota_cache.json'
        if data is not None:
            self.write(data)

    def write(self, data):
        text = data if isinstance(data, str) else json.dumps(data)
        self.path.write_text(text)

    def cleanup(self):
        self.dir.cleanup()








class CacheTests(unittest.IsolatedAsyncioTestCase):
    async def test_refresh_single_flight_minimum_interval_and_cancel(self):
        cache = TempCache(fixture())
        self.addCleanup(cache.cleanup)
        clock = Clock(FETCHED)
        refresh = FakeRefresher(delay=.02)
        source = quota.QuotaSource(cache.path, refresher=refresh, mono=lambda: clock.mono, log=lambda *_: None)
        self.addAsyncCleanup(source.close)
        source._maybe_refresh()
        source._maybe_refresh()
        await source.task
        self.assertEqual(refresh.calls, 1)
        source._maybe_refresh()
        self.assertEqual(refresh.calls, 1)
        clock.advance(quota.MIN_REFRESH_INTERVAL_S)
        source._maybe_refresh()
        await source.task
        self.assertEqual(refresh.calls, 2)
        clock.advance(quota.MIN_REFRESH_INTERVAL_S)
        source._maybe_refresh()
        await source.close()
        self.assertTrue(source.task.cancelled())

    async def test_refresh_failure_is_bounded_and_value_free(self):
        cache = TempCache(fixture())
        self.addCleanup(cache.cleanup)
        messages = []
        async def fail(): raise ValueError('secret payload')
        source = quota.QuotaSource(cache.path, refresher=fail, log=messages.append)
        source._maybe_refresh()
        await source.task
        self.assertEqual(source.last_result, 'error-ValueError')
        self.assertNotIn('secret payload', repr(messages))

    def test_cache_rejects_missing_bad_json_symlink_oversize_and_nonregular(self):
        cache = TempCache()
        self.addCleanup(cache.cleanup)
        self.assertEqual(quota.read_cache(cache.path), (None, None))
        for value in ('{', json.dumps({'providers': []}), ' ' * (quota.MAX_CACHE_BYTES + 1)):
            cache.path.write_text(value)
            self.assertEqual(quota.read_cache(cache.path), (None, None))
        cache.write(fixture())
        self.assertEqual(quota.read_cache(cache.path)[1], FETCHED)
        link = cache.path.with_name('link')
        link.symlink_to(cache.path)
        self.assertEqual(quota.read_cache(link), (None, None))
        self.assertEqual(quota.read_cache(cache.path.parent), (None, None))


class SubprocessRefresherTests(unittest.IsolatedAsyncioTestCase):
    async def test_runs_official_sweep_in_fresh_interpreter_without_hermes_home(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            home = pathlib.Path(tmp)
            plugin = home / 'plugins' / 'quota'
            plugin.mkdir(parents=True)
            (plugin / '__init__.py').write_text('')
            # Stand-in for the official module: records its environment instead of fetching.
            (plugin / 'quota_cache.py').write_text(
                'import json, os, sys\n'
                'def refresh_quota_cache():\n'
                '    out = os.path.join(os.path.dirname(__file__), "ran.json")\n'
                '    json.dump({"hermes_home": os.environ.get("HERMES_HOME"), "argv": sys.argv}, open(out, "w"))\n')
            import sys
            refresher = quota.SubprocessRefresher(home / 'quota_cache.json', python=sys.executable,
                                                  default_home=home, timeout=20)
            os.environ['HERMES_HOME'] = '/nonexistent-leak'
            try:
                self.assertEqual(await refresher(), 'ok')
            finally:
                del os.environ['HERMES_HOME']
            ran = json.loads((plugin / 'ran.json').read_text())
            self.assertIsNone(ran['hermes_home'])  # default home => HERMES_HOME unset in the child

    async def test_source_checkout_top_level_dependencies_are_importable(self):
        # Editable installs expose agent but can omit its top-level hermes_yaml dependency.
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            home = pathlib.Path(tmp)
            plugin = home / 'plugins' / 'quota'
            plugin.mkdir(parents=True)
            (plugin / '__init__.py').write_text('')
            source = home / 'hermes-agent'
            source.mkdir()
            (source / 'quota_dependency_probe.py').write_text('VALUE = 42\n')
            (plugin / 'quota_cache.py').write_text(
                'def refresh_quota_cache():\n'
                '    from quota_dependency_probe import VALUE\n'
                '    assert VALUE == 42\n')
            import sys
            refresher = quota.SubprocessRefresher(home / 'quota_cache.json', python=sys.executable,
                                                  default_home=home, timeout=20)
            self.assertEqual(await refresher(), 'ok')

    async def test_timeout_kills_the_child(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            home = pathlib.Path(tmp)
            plugin = home / 'plugins' / 'quota'
            plugin.mkdir(parents=True)
            (plugin / '__init__.py').write_text('')
            (plugin / 'quota_cache.py').write_text('import time\ndef refresh_quota_cache():\n    time.sleep(30)\n')
            import sys
            refresher = quota.SubprocessRefresher(home / 'quota_cache.json', python=sys.executable,
                                                  default_home=home, timeout=0.5)
            self.assertEqual(await refresher(), 'timeout')

    async def test_missing_plugin_or_python_is_reported_not_raised(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            home = pathlib.Path(tmp)
            self.assertEqual(await quota.SubprocessRefresher(home / 'quota_cache.json', python='/bin/sh',
                                                             default_home=home)(), 'plugin-missing')
            (home / 'plugins' / 'quota').mkdir(parents=True)
            (home / 'plugins' / 'quota' / 'quota_cache.py').write_text('')
            self.assertEqual(await quota.SubprocessRefresher(home / 'quota_cache.json',
                                                             python=str(home / 'no-python'),
                                                             default_home=home)(), 'python-missing')




class ConfigTests(unittest.TestCase):
    def test_default_cache_path_follows_hermes_home_without_gateway(self):
        from waveshare_bridge import config
        self.assertEqual(config.default_quota_cache({}), pathlib.Path.home() / '.hermes' / 'quota_cache.json')
        self.assertEqual(config.default_quota_cache({'HERMES_HOME': '/tmp/hh'}), pathlib.Path('/tmp/hh/quota_cache.json'))


if __name__ == '__main__':
    unittest.main()
