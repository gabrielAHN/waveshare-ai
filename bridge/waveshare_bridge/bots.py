"""WBT1 bots from authenticated plugin capabilities and optional quota telemetry.

Only verified quota exhaustion blocks a routable bot. Missing telemetry or a
usage-check sign-in failure cannot disable it. Unreachable or missing /bots
capability makes every bot unavailable. Wire layout and reason numbers are fixed.
"""
import asyncio
import json
import math
import os
import pathlib
import re
import stat
import struct
import time
import zlib

from . import quota as quota_mod

MAGIC = b'WBT1'
VERSION = 1
MAX_BOTS = 3
TEXT = 12
BOT_FMT = f'<{TEXT}s{TEXT}s{TEXT}sBBBBI'
HEADER_FMT = '<4sBBBB'
FRAME_SIZE = struct.calcsize(HEADER_FMT) + MAX_BOTS * struct.calcsize(BOT_FMT) + 4
NONE32 = 0xFFFFFFFF
# phone_auth: the board is not signed in with the phone (phone_pair.py); set per board by the bridge.
REASONS = {'none': 0, 'exhausted': 1, 'signin': 2, 'plugin_update': 3, 'phone_auth': 4,
           'upstream': 5}
REASON_NAMES = {v: k for k, v in REASONS.items()}
FLAG_PLUGIN_BOTS = 1
FLAG_QUOTA_STALE = 2
FLAG_PLUGIN_UNKNOWN = 4
BOT_STALE = 1
DEFAULT_BOTS = ('helper', 'atlas', 'coding')
PROBE_TTL_S = 60.0
MAX_CONFIG_BYTES = 262144
# WBT1 reserves one of its 12 text bytes for NUL; ids are never truncated.
_ID = re.compile(r'[a-z0-9][a-z0-9_-]{0,10}')
_PROVIDER = re.compile(r'[a-z0-9][a-z0-9._-]{0,31}')
PROVIDER_LABELS = {'anthropic': 'Claude', 'openai-codex': 'Codex', 'openrouter': 'OpenRouter',
                   'copilot': 'Copilot', 'gemini': 'Gemini', 'nous': 'Nous', 'kimi': 'Kimi', 'zai': 'Z.ai'}
PROVIDER_ALIASES = {'claude': 'anthropic', 'codex': 'openai-codex'}


def valid_bot(name):
    return type(name) is str and bool(_ID.fullmatch(name)) and name != 'default'


def _read_private_text(path):
    """Bounded read that never follows a symlink; None on any problem."""
    try:
        fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    except OSError:
        return None
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size > MAX_CONFIG_BYTES:
            return None
        with os.fdopen(fd, 'rb', closefd=False) as handle:
            raw = handle.read(MAX_CONFIG_BYTES + 1)
    except OSError:
        return None
    finally:
        os.close(fd)
    if len(raw) > MAX_CONFIG_BYTES:
        return None
    return raw.decode('utf-8', 'replace')


def _scalar(text):
    text = text.split(' #', 1)[0].strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in '\'"':
        text = text[1:-1]
    return text.strip()


def profile_provider(profiles_dir, bot):
    """``model.provider`` of one profile's config.yaml (block form only), or None."""
    if not valid_bot(bot):
        return None
    text = _read_private_text(pathlib.Path(profiles_dir) / bot / 'config.yaml')
    if text is None:
        return None
    in_model, indent = False, None
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if not line[0].isspace():
            if in_model:
                break
            in_model = line.rstrip() == 'model:'
            continue
        if not in_model:
            continue
        lead = len(line) - len(line.lstrip())
        indent = lead if indent is None else indent
        if lead != indent:
            continue  # nested deeper than the model block's own keys
        key, sep, value = line.strip().partition(':')
        if sep and key == 'provider':
            value = _scalar(value).lower()
            value = PROVIDER_ALIASES.get(value, value)
            return value if _PROVIDER.fullmatch(value) else None
    return None


def provider_label(slug):
    if not slug:
        return ''
    return PROVIDER_LABELS.get(slug, slug[:1].upper() + slug[1:])[:TEXT - 1]


def _credits(details):
    for line in details if isinstance(details, list) else []:
        if isinstance(line, str) and line.lower().startswith('credits balance'):
            match = re.search(r'(-)?\$?\s*(-)?\s*([0-9][0-9,]*(?:\.[0-9]+)?)', line.split(':', 1)[-1])
            if match:
                try:
                    value = float(match.group(3).replace(',', ''))
                except ValueError:
                    return None
                return -value if (match.group(1) or match.group(2)) else value
    return None


def assess(record, now):
    """One fresh official-cache provider record. Anything inconclusive fails closed."""
    if not isinstance(record, dict):
        return False, 'upstream', None
    reason = record.get('unavailable_reason')
    if reason in quota_mod.AUTH_REASONS:
        return False, 'signin', None
    if reason:
        return False, 'upstream', None
    if not isinstance(record.get('windows'), list):
        return False, 'upstream', None
    blocking, unknown_reset = [], False
    for w in record.get('windows') if isinstance(record.get('windows'), list) else []:
        if not isinstance(w, dict):
            return False, 'upstream', None
        used = w.get('used_percent')
        if type(used) not in (int, float) or isinstance(used, bool) or not math.isfinite(used):
            return False, 'upstream', None
        if used < 100:
            continue
        reset = quota_mod._parse_time(w.get('reset_at'))
        if reset is None or reset <= now:
            # A scheduled reset is not proof the provider has replenished quota.
            # Keep blocked until a fresh cache reports actual usage below 100%.
            unknown_reset = True
        else:
            blocking.append(int(reset - now))
    if blocking:
        return False, 'exhausted', max(blocking)
    if unknown_reset:
        return False, 'exhausted', None
    credits = _credits(record.get('details'))
    if credits is not None and credits <= 0:
        return False, 'exhausted', None
    return True, 'none', None


def _pad(text):
    return quota_mod.ascii_text(text, TEXT - 1).encode('ascii').ljust(TEXT, b'\0')


def encode_frame(bots, flags):
    if len(bots) > MAX_BOTS or type(flags) is not int or flags & ~7:
        raise ValueError('invalid bots frame')
    out = bytearray(struct.pack(HEADER_FMT, MAGIC, VERSION, len(bots), flags, 0))
    for i in range(MAX_BOTS):
        if i < len(bots):
            b = bots[i]
            if not valid_bot(b.get('id')):
                raise ValueError('invalid bot id: expected 1..11 profile characters (not default)')
            reset = b.get('reset_in_s')
            bid = b['id'].encode('ascii').ljust(TEXT, b'\0')
            out += struct.pack(BOT_FMT, bid, _pad(b['name']), _pad(b.get('provider_label', '')),
                               1 if b['available'] else 0, REASONS[b['reason']], BOT_STALE if b.get('stale') else 0,
                               0, NONE32 if reset is None else max(0, min(NONE32 - 1, int(reset))))
        else:
            out += struct.pack(BOT_FMT, b'\0' * TEXT, b'\0' * TEXT, b'\0' * TEXT, 0, 0, 0, 0, NONE32)
    out += struct.pack('<I', zlib.crc32(out))
    assert len(out) == FRAME_SIZE
    return bytes(out)


def decode_frame(data):
    """Strict reference decoder (mirrors firmware bots_view.h)."""
    if type(data) is not bytes or len(data) != FRAME_SIZE:
        raise ValueError('bad size')
    if struct.unpack('<I', data[-4:])[0] != zlib.crc32(data[:-4]):
        raise ValueError('bad crc')
    magic, version, count, flags, zero = struct.unpack_from(HEADER_FMT, data, 0)
    if magic != MAGIC or version != VERSION or count > MAX_BOTS or zero or flags & ~7:
        raise ValueError('bad header')
    offset, bots = struct.calcsize(HEADER_FMT), []
    for i in range(MAX_BOTS):
        bid, name, prov, avail, reason, bflags, pad, reset = struct.unpack_from(BOT_FMT, data, offset)
        offset += struct.calcsize(BOT_FMT)
        if i >= count:
            continue
        if avail > 1 or reason not in REASON_NAMES or bflags & ~BOT_STALE or pad:
            raise ValueError('bad bot')
        bots.append({'id': quota_mod._text(bid), 'name': quota_mod._text(name), 'provider_label': quota_mod._text(prov),
                     'available': bool(avail), 'reason': REASON_NAMES[reason], 'stale': bool(bflags & BOT_STALE),
                     'reset_in_s': None if reset == NONE32 else reset})
    return {'flags': flags, 'bots': bots}


class CapabilityClient:
    """Only GET /bots, with its scoped machine credential and one authentication retry."""
    def __init__(self, base, credentials, client):
        from .common import local_base
        self.base, self.credentials, self.client = local_base(base), credentials, client

    async def bots(self):
        import aiohttp
        from .common import Unavailable, parse_json
        for attempt in (0, 1):
            try:
                token = await self.credentials.token()
                async with self.client.get(self.base + '/api/plugins/waveshare-sessions/bots',
                        headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/json'},
                        allow_redirects=False, timeout=aiohttp.ClientTimeout(total=3)) as response:
                    status = response.status
                    if status in (401, 403) and attempt == 0:
                        self.credentials.invalidate()
                        continue
                    if response.content_type != 'application/json':
                        return status, None
                    data = bytearray()
                    async for chunk in response.content.iter_chunked(8192):
                        data.extend(chunk)
                        if len(data) > 65536:
                            return 502, None
                    return status, parse_json(data)
            except (aiohttp.ClientError, asyncio.TimeoutError, OSError, ValueError, Unavailable):
                return 503, None
        return 503, None


class PluginProbe:
    """Cached authenticated /bots capability; missing routes make every bot unavailable."""

    def __init__(self, client, mono=time.monotonic, ttl=PROBE_TTL_S, log=None):
        self.client, self.mono, self.ttl = client, mono, ttl
        self.log = log or (lambda message: print(message, flush=True))
        self.supported = None   # None unknown, tuple of ids when supported
        self.checked = None
        self.task = None
        self.lock = asyncio.Lock()
        self.announced = object()

    async def refresh(self):
        async with self.lock:
            status, body = await self.client.bots()
            self.checked = self.mono()
            self.supported = None
            if (status == 200 and isinstance(body, dict) and type(body.get('version')) is int
                    and body['version'] == 1 and isinstance(body.get('bots'), list)
                    and len(body['bots']) <= 8 and all(valid_bot(b) for b in body['bots'])
                    and len(set(body['bots'])) == len(body['bots'])):
                self.supported = tuple(body['bots'])
            summary = 'bots ' + ','.join(self.supported) if isinstance(self.supported, tuple) else 'unknown'
            if summary != self.announced:
                self.announced = summary
                self.log(f'Hermes plugin capability: {summary}.')
            return self.supported

    def maybe_refresh(self):
        if self.client is None or (self.task is not None and not self.task.done()):
            return
        if self.checked is not None and self.mono() - self.checked < max(0, self.ttl - 8):
            return
        # Refresh BEFORE expiry; retain valid evidence while the request is in flight.
        # routable() independently enforces the hard TTL, and failures clear support.
        if self.checked is None or self.mono() - self.checked >= self.ttl:
            self.supported = None
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            return
        self.task = loop.create_task(self.refresh())

    def routable(self, bot):
        if self.checked is None or not 0 <= self.mono() - self.checked < self.ttl:
            return False
        if isinstance(self.supported, tuple):
            return bot in self.supported
        return False

    async def close(self):
        if self.task is not None and not self.task.done():
            self.task.cancel()
            try:
                await self.task
            except (asyncio.CancelledError, Exception):
                pass


class BotSource:
    def __init__(self, cache_path, profiles_dir, bots=DEFAULT_BOTS, providers=None, names=None, probe=None,
                 quota_source=None, wall=time.time, stale_s=quota_mod.STALE_S, log=None, gate_path=None):
        bots = tuple(bots)
        if not 1 <= len(bots) <= MAX_BOTS or not all(valid_bot(b) for b in bots) or len(set(bots)) != len(bots):
            raise ValueError('bots must be 1..3 distinct profile ids of 1..11 characters (not default)')
        self.cache_path = pathlib.Path(cache_path).expanduser() if cache_path else None
        self.profiles_dir = pathlib.Path(profiles_dir).expanduser()
        self.bots, self.providers, self.names = bots, dict(providers or {}), dict(names or {})
        self.probe, self.quota_source, self.wall, self.stale_s = probe, quota_source, wall, stale_s
        self.log = log or (lambda message: print(message, flush=True))
        self.announced = None
        self.gate_path = pathlib.Path(gate_path) if gate_path else None
        self.blocks = {}
        if self.gate_path and self.gate_path.exists():
            raw = _read_private_text(self.gate_path)
            saved = json.loads(raw) if raw is not None else None
            if not isinstance(saved, dict) or any(not _PROVIDER.fullmatch(k) or v != 'exhausted' for k,v in saved.items()):
                raise ValueError('invalid persisted quota blocks')
            self.blocks = dict(saved)

    def provider(self, bot):
        override = self.providers.get(bot)
        if isinstance(override, str) and _PROVIDER.fullmatch(override.lower()):
            return override.lower()
        return profile_provider(self.profiles_dir, bot)

    def snapshot(self):
        now = self.wall()
        cache, fetched = quota_mod.read_cache(self.cache_path) if self.cache_path else (None, None)
        stale = cache is None or now - fetched > self.stale_s
        if stale and self.quota_source is not None:
            self.quota_source._maybe_refresh()   # bounded single-flight fresh-interpreter telemetry refresh
        if self.probe is not None:
            self.probe.maybe_refresh()
        providers = cache.get('providers') if cache is not None else {}
        out = []
        old_blocks = dict(self.blocks)
        for bot in self.bots:
            slug = self.provider(bot)
            record = providers.get(slug) if slug else None
            available, reason, reset = assess(record, now)
            if reason == 'signin':
                # The official usage check could not read this provider's quota (its own login or
                # token). That is not Hermes sign-in: Hermes routes the bot through its authenticated
                # gateway and the board is signed in by phone. Never grey the bot out for it; the live
                # route (below) decides, like any other missing quota telemetry.
                available, reason, reset = False, 'upstream', None
            bot_stale = stale or reason == 'upstream'
            if reason == 'upstream' and self.quota_source is not None:
                # A freshly written error record is not fresh quota. Another cache
                # writer may repeatedly fail imports; use our bounded single-flight
                # refresher rather than waiting forever for the file to become old.
                self.quota_source._maybe_refresh()
            # Never create a new refusal from expired telemetry (including a cache
            # replay after recovery). A previously verified block still survives errors.
            if stale:
                available, reason, reset = False, 'upstream', None
            if slug and not stale and reason == 'exhausted':
                self.blocks[slug] = reason
            elif slug and reason == 'none' and not stale and isinstance(record, dict) and (record.get('windows') or (_credits(record.get('details')) or 0) > 0):
                self.blocks.pop(slug, None)
            elif slug in self.blocks:
                available, reason, reset = False, self.blocks[slug], None
            # Quota telemetry is optional; it is not backend health. Preserve explicit
            # exhaustion evidence even after reset/cache expiry, but missing or
            # errored telemetry cannot veto a separately authenticated live route.
            if reason == 'upstream' or (stale and reason == 'none'):
                available = self.probe is not None and self.probe.routable(bot)
                reason = 'none' if available else 'upstream'
                reset = None
            if self.probe is None or not self.probe.routable(bot):
                available, reason, reset = False, 'upstream', None
            name = self.names.get(bot) if isinstance(self.names.get(bot), str) else None
            out.append({'id': bot, 'name': (name or bot[:1].upper() + bot[1:])[:TEXT - 1], 'provider': slug or '',
                        'provider_label': provider_label(slug), 'available': available, 'reason': reason,
                        'reset_in_s': reset, 'stale': bot_stale})
        if self.gate_path and self.blocks != old_blocks:
            from .enroll import write_private_json
            write_private_json(self.gate_path, self.blocks)
        flags = FLAG_QUOTA_STALE if stale else 0
        if self.probe is not None:
            if isinstance(self.probe.supported, tuple):
                flags |= FLAG_PLUGIN_BOTS
            elif self.probe.supported is None:
                flags |= FLAG_PLUGIN_UNKNOWN
        summary = ' '.join(f"{b['id']}={b['provider'] or '?'}:{'ok' if b['available'] else 'blocked'}"
                           f"/{b['reason']}{'/stale' if b['stale'] else ''}" for b in out)
        if summary != self.announced:
            self.announced = summary
            self.log(f'Bots: {summary}.')
        return {'flags': flags, 'bots': out}

    def frame(self):
        snap = self.snapshot()
        return encode_frame(snap['bots'], snap['flags'])

    async def close(self):
        if self.probe is not None:
            await self.probe.close()
