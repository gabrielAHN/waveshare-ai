"""Optional official quota telemetry: bounded cache reads and single-flight refresh.

The official Hermes quota plugin owns provider fetches. The bridge reads its cache
and runs a bounded sweep in a fresh interpreter when telemetry needs refreshing.
BotSource alone decides availability from capability and verified exhaustion.
"""
import asyncio
import json
import os
import pathlib
import stat
import time
from datetime import datetime, timezone

AUTH_REASONS = frozenset({'no-credentials', 'not-logged-in'})
STALE_S = 300
MIN_REFRESH_INTERVAL_S = 120
REFRESH_TIMEOUT_S = 60
MAX_CACHE_BYTES = 262144

def ascii_text(value, limit):
    """Printable ASCII only, whitespace collapsed, bounded. Anything else is dropped."""
    text = ''.join(c if 32 <= ord(c) < 127 else ' ' for c in str(value or ''))
    return ' '.join(text.split())[:limit].rstrip()


def _parse_time(value):
    if not isinstance(value, str) or len(value) > 64:
        return None
    try:
        dt = datetime.fromisoformat(value)
    except ValueError:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.timestamp()


def _text(raw):
    if raw[-1:] != b'\0':
        raise ValueError('unterminated text')
    text = raw.split(b'\0', 1)[0]
    if any(c < 32 or c > 126 for c in text):
        raise ValueError('non-ASCII text')
    return text.decode('ascii')


def read_cache(path):
    """Parsed cache dict + fetched_at epoch, or (None, None). Never follows a symlink."""
    try:
        fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    except OSError:
        return None, None
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size > MAX_CACHE_BYTES:
            return None, None
        with os.fdopen(fd, 'rb', closefd=False) as handle:
            raw = handle.read(MAX_CACHE_BYTES + 1)
    except OSError:
        return None, None
    finally:
        os.close(fd)
    if len(raw) > MAX_CACHE_BYTES:
        return None, None
    try:
        data = json.loads(raw)
    except (ValueError, RecursionError):
        return None, None
    if not isinstance(data, dict) or not isinstance(data.get('providers'), dict):
        return None, None
    fetched = _parse_time(data.get('fetched_at'))
    if fetched is None:
        return None, None
    return data, fetched


def default_python():
    home = pathlib.Path.home() / '.hermes' / 'hermes-agent'
    for candidate in (home / 'venv' / 'bin' / 'python', home / '.venv' / 'bin' / 'python'):
        if os.access(candidate, os.X_OK):
            return str(candidate)
    return None


class SubprocessRefresher:
    """Run the official plugin's ``refresh_quota_cache()`` in a FRESH interpreter.

    A fresh interpreter loads current provider credentials for each sweep.
    The plugin's own REFRESH_BUDGET_S bounds the sweep; ``timeout`` is the hard backstop.
    ``HERMES_HOME`` is removed from the child's environment when the cache lives in the
    default home (~/.hermes), and set to the cache's directory otherwise. Output is discarded."""

    def __init__(self, cache_path, python=None, default_home=None, timeout=REFRESH_TIMEOUT_S):
        self.home = pathlib.Path(cache_path).expanduser().parent
        self.python = python if python is not None else default_python()
        self.default_home = pathlib.Path(default_home) if default_home else pathlib.Path.home() / '.hermes'
        self.timeout = float(timeout)

    async def __call__(self):
        plugin = self.home / 'plugins' / 'quota'
        if not (plugin / 'quota_cache.py').is_file():
            return 'plugin-missing'
        if not self.python or not os.access(self.python, os.X_OK):
            return 'python-missing'
        env = {k: v for k, v in os.environ.items() if k != 'HERMES_HOME'}
        if self.home.resolve() != self.default_home.resolve():
            env['HERMES_HOME'] = str(self.home)
        # Core's editable install may expose agent/ but not top-level modules such
        # as hermes_yaml. Use the same source root as the supported CLI launch.
        source = self.default_home / 'hermes-agent'
        code = ('import sys; sys.path.insert(0, %r); sys.path.insert(0, %r); '
                'from quota.quota_cache import refresh_quota_cache; refresh_quota_cache()') % (str(source), str(plugin.parent))
        try:
            proc = await asyncio.create_subprocess_exec(
                self.python, '-c', code, env=env, cwd=str(self.home), stdin=asyncio.subprocess.DEVNULL,
                stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL, start_new_session=True)
        except OSError:
            return 'spawn-failed'
        try:
            code = await asyncio.wait_for(proc.wait(), timeout=self.timeout)
        except asyncio.TimeoutError:
            proc.kill()
            await proc.wait()
            return 'timeout'
        except asyncio.CancelledError:
            proc.kill()
            raise
        return 'ok' if code == 0 else 'failed'


class QuotaSource:
    """Non-blocking single-flight telemetry refresh with a hard minimum interval."""

    def __init__(self, path, refresher=None, wall=time.time, mono=time.monotonic, log=None,
                 stale_s=STALE_S, min_interval_s=MIN_REFRESH_INTERVAL_S):
        self.path = pathlib.Path(path).expanduser()
        self.refresher = refresher if refresher is not None else SubprocessRefresher(self.path)
        self.wall, self.mono = wall, mono
        self.log = log or (lambda message: print(message, flush=True))
        self.stale_s, self.min_interval_s = stale_s, min_interval_s
        self.task = None
        self.last_attempt = None
        self.last_result = None

    def _maybe_refresh(self):
        if self.task is not None and not self.task.done():
            return
        now = self.mono()
        if self.last_attempt is not None and now - self.last_attempt < self.min_interval_s:
            return
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            return
        self.last_attempt = now
        self.task = loop.create_task(self._refresh())

    async def _refresh(self):
        started = self.mono()
        try:
            result = await self.refresher()
        except Exception as error:  # never let a refresher bug reach the request path
            result = 'error-' + type(error).__name__
        self.last_result = result
        self.log(f'Quota cache refresh (fresh interpreter): {result} in {self.mono() - started:.1f} s.')



    async def close(self):
        if self.task is not None and not self.task.done():
            self.task.cancel()
            try:
                await self.task
            except asyncio.CancelledError:
                pass
