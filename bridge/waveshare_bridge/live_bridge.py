"""TLS enrolled-board API and read-only machine-authenticated Sparkles feed."""
import hashlib
import hmac
import math
import pathlib
import re
import struct
import time
from aiohttp import web
import aiohttp
import asyncio
from .common import Unavailable, local_base, parse_json

DEFAULT_THRESHOLDS = (20000, 100000, 200000, 350000, 600000)  # tokens/min: L1..L5 lower bounds
LEVELS = 6
FLAG_MEASURED = 1
FLAG_DEGRADED = 2
PROVIDER_CODES = {'anthropic': 1, 'openai-codex': 2, 'openrouter': 3}
MAX_TOKENS = 10 ** 15


USAGE_ROUTE = '/api/plugins/waveshare-sessions/usage'
_UPSTREAM_MESSAGES = {
    'ready': 'Bridge upstream ready (OAuth2 client credential).',
    'waiting-authelia': ('Bridge upstream: waiting for the sign-in provider client (token endpoint '
                         'rejected the Hermes plugin client or is unreachable); retrying with backoff.'),
    'waiting-plugin': ('Bridge upstream: waiting for plugin route (dashboard refused or lacks '
                       + USAGE_ROUTE + '); retrying with backoff.'),
    'unavailable': 'Bridge upstream: dashboard unavailable (unreachable or 5xx); retrying with backoff.',
}


class UsageGateway:
    """Machine-credential upstream: GET the waveshare-sessions plugin route with an OAuth2
    client_credentials bearer from your sign-in provider. Sample/DensityTracker/WLS4 pipeline; no WS, no
    interactive login, no refresh token. Only USAGE_ROUTE is ever requested."""

    def __init__(self, base, credentials, sample, client, usage_interval=5.0):
        self.base = local_base(base)
        self.credentials, self.sample, self.client = credentials, sample, client
        self.usage_interval = float(usage_interval)
        self.last_usage = None
        self.state = None
        self.working_ids = []
        self.providers = {}  # sid -> (verified runtime slug, monotonic arrival)

    def provider_rows(self, rows, with_tokens, now):
        """Reuse route evidence between 5s status sweeps, never beyond the 6s live TTL.

        Roster-only replies intentionally omit provider. They must not cause color flicker,
        extend provider freshness, resurrect removed sessions, or conceal a failed status read.
        """
        current = {r['id'] for r in rows if r['status'] == 'working'}
        self.providers = {sid: value for sid, value in self.providers.items()
                          if sid in current and 0 <= now - value[1] < 6.0}
        out = []
        for row in rows:
            item = dict(row)
            if with_tokens and row['id'] in current:
                self.providers.pop(row['id'], None)
                if row.get('provider') in PROVIDER_CODES:
                    self.providers[row['id']] = (row['provider'], now)
            evidence = self.providers.get(row['id'])
            item['provider'] = evidence[0] if evidence else None
            out.append(item)
        return out

    async def close(self):
        return None

    async def _get(self, token, with_tokens):
        url = self.base + USAGE_ROUTE + ('?tokens=1' if with_tokens else '')
        async with self.client.get(url, headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/json'},
                                   allow_redirects=False, timeout=aiohttp.ClientTimeout(total=3)) as response:
            if response.status != 200 or response.content_type != 'application/json':
                return response.status, None
            data = bytearray()
            async for chunk in response.content.iter_chunked(8192):
                data.extend(chunk)
                if len(data) > 262144:
                    raise Unavailable()
            return 200, parse_json(data)

    @staticmethod
    def _rows(value, with_tokens):
        """[(id, status)] + {id: tokens|None} from the plugin body; ValueError on any surprise."""
        if type(value) is not dict or value.get('version') != 1 or type(value.get('sessions')) is not list:
            raise ValueError('invalid usage body')
        rows, totals = [], {}
        for item in value['sessions']:
            if type(item) is not dict or type(item.get('id')) is not str or type(item.get('status')) is not str:
                raise ValueError('invalid usage row')
            provider = item.get('provider')
            if provider is not None and type(provider) is not str:
                raise ValueError('invalid usage row')
            rows.append({'id': item['id'], 'status': item['status'], 'provider': provider})
            if with_tokens and item['status'] == 'working':
                tokens = item.get('tokens')
                totals[item['id']] = tokens if type(tokens) is int and 0 <= tokens <= MAX_TOKENS else None
        return rows, totals

    async def _poll(self):
        from .authelia_client import ClientRejected
        now = time.monotonic()
        with_tokens = self.last_usage is None or now - self.last_usage >= self.usage_interval
        try:
            token = await self.credentials.token()
        except (ClientRejected, Unavailable):
            return 'waiting-authelia'
        status, value = await self._get(token, with_tokens)
        if status in (401, 403):
            # One retry with a freshly minted token (key rotation / Authelia restart); then wait.
            self.credentials.invalidate()
            try:
                token = await self.credentials.token()
            except (ClientRejected, Unavailable):
                return 'waiting-authelia'
            status, value = await self._get(token, with_tokens)
        if status in (401, 403, 404):
            return 'waiting-plugin'
        if status != 200:
            return 'unavailable'
        rows, totals = self._rows(value, with_tokens)
        rows = self.provider_rows(rows, with_tokens, time.monotonic())
        if not self.sample.update(rows):
            raise Unavailable()
        self.working_ids = [row['id'] for row in rows if row['status'] == 'working']
        self.sample.auth_expires = self.credentials.expires_at
        if with_tokens:
            self.last_usage = now
            if self.working_ids:
                self.sample.tracker.observe(totals, time.monotonic())
                self.sample.relevel()
        return 'ready'

    async def poll_once(self):
        try:
            state = await asyncio.wait_for(self._poll(), timeout=8.0)
        except (aiohttp.ClientError, asyncio.TimeoutError, OSError, ValueError,
                TypeError, RecursionError, Unavailable):
            state = 'unavailable'
        if state != 'ready':
            self.sample.invalidate()
            self.working_ids = []
            self.providers.clear()
            self.last_usage = None
        changed = state != self.state
        self.state = state
        if changed:
            print(_UPSTREAM_MESSAGES[state], flush=True)
        return state == 'ready'

    async def run(self):
        backoff = 1.0
        try:
            while True:
                started = time.monotonic()
                ok = await self.poll_once()
                if ok:
                    change = self.sample.tracker.announce()
                    if change:
                        print(change, flush=True)
                delay = 1.0 if ok else backoff
                backoff = 1.0 if ok else min(backoff * 2, 30.0)
                await asyncio.sleep(max(0.0, delay - (time.monotonic() - started)))
        finally:
            self.sample.invalidate()


def parse_thresholds(text):
    values = tuple(int(part) for part in str(text).split(','))
    if (len(values) != LEVELS - 1 or values[0] < 1 or values[-1] > 10 ** 9
            or any(b <= a for a, b in zip(values, values[1:]))):
        raise ValueError('need 5 strictly increasing positive token/min thresholds')
    return values


class DensityTracker:
    """Aggregate token throughput of WORKING sessions -> level 0..5 (the Sparkles "range of use").

    Per-session rate = the positive token deltas that arrived in the last ``ema_seconds`` (the
    window, default 60 s) spread over the time they cover, so one 50k-token model call reads as
    ~50k tok/min for a minute instead of a 5 s spike to the top level and back to zero. Compression/reset (negative delta) clears
    that session's history, a newly seen session only sets a baseline, and idle/removal/outage
    drops its baseline and rate immediately. Levels use DEFAULT_THRESHOLDS (measured Hermes
    sessions run ~125k-430k tok/min, so one session spans levels 2-4 and several reach 5); a
    level falls only below hysteresis*threshold; no working sessions => zero."""
    def __init__(self, thresholds=DEFAULT_THRESHOLDS, ema_seconds=60.0, hysteresis=0.8, stale_seconds=30.0):
        self.thresholds = parse_thresholds(','.join(str(t) for t in thresholds))
        if not 0.5 <= float(ema_seconds) <= 600 or not 0.3 <= hysteresis < 1 or stale_seconds <= 0:
            raise ValueError('invalid smoothing parameters')
        self.ema_seconds, self.hysteresis, self.stale_seconds = float(ema_seconds), hysteresis, stale_seconds
        self.baselines = {}  # runtime sid -> (total, monotonic time); never forwarded
        self.history = {}  # sid -> [(previous sample time, sample time, positive delta)] inside the window
        self.rates = {}  # sid -> current windowed tokens/min
        self.working = 0
        self.ema = 0.0
        self.level = 0
        self.last_update = None
        self.measured_at = None
        self.degraded = False
        self.announced = None

    def roster(self, rows):
        """Sessions flip working -> idle between turns (seen live: every few seconds, 4 -> 0 -> 5 ...),
        so a listed session that is idle keeps its last minute of usage in the window and the range
        holds while others work; only its baseline goes, so a re-entry never counts lifetime tokens.
        Nothing working => calm (the board shows no session sparkle); an outage clears everything."""
        present = {row['id'] for row in rows if row['status'] == 'working'}
        known = {row['id'] for row in rows}
        for sid in list(self.baselines):
            if sid not in present:
                del self.baselines[sid]
        for sid in list(self.history):
            if sid not in known:
                self.history.pop(sid, None)
                self.rates.pop(sid, None)
        self.working = sum(1 for row in rows if row['status'] == 'working')
        if not rows:
            self.clear()
            return
        self.ema = sum(self.rates.values())
        self.level = self.classify(self.ema)

    def clear(self):
        self.baselines.clear()
        self.history.clear()
        self.rates.clear()
        self.working = 0
        self.ema, self.level, self.last_update, self.degraded = 0.0, 0, None, False
        self.measured_at = None

    def classify(self, value):
        level = sum(1 for t in self.thresholds if value >= t)
        if level >= self.level:
            return level
        level = self.level
        while level > 0 and value < self.thresholds[level - 1] * self.hysteresis:
            level -= 1
        return level

    def _window_rate(self, sid, now):
        window = self.ema_seconds
        kept = [h for h in self.history.get(sid, ()) if h[1] > now - window]
        self.history[sid] = kept
        if not kept:
            return 0.0
        covered = max(window, now - min(h[0] for h in kept))
        return sum(h[2] for h in kept) * 60.0 / covered

    def observe(self, totals, now):
        measured, failed = False, False
        for sid, total in totals.items():
            if total is None:
                failed = True
                continue
            previous = self.baselines.get(sid)
            if previous is not None and now <= previous[1]:
                continue  # never rewrite a monotonic baseline with an earlier sample
            self.baselines[sid] = (total, now)
            if previous is None:
                continue
            delta = total - previous[0]
            if delta < 0:
                self.history[sid] = []  # compression/reset: not throughput
            elif delta > 0:
                self.history.setdefault(sid, []).append((previous[1], now, delta))
            measured = True
        if measured:
            for sid in list(self.history):
                self.rates[sid] = self._window_rate(sid, now)  # idle sessions age out of the window too
        self.degraded = failed
        if not measured:
            return
        self.ema = sum(self.rates.values())
        self.last_update = self.measured_at = now
        self.level = self.classify(self.ema)

    def fail(self):
        self.degraded = True

    def wire(self, now=None):
        """(level, flags) for the WLS4 header."""
        now = time.monotonic() if now is None else now
        if not self.working:
            return 0, 0
        if self.measured_at is None and not self.degraded:
            return 0, 0  # warming up: first sweep only sets baselines
        if self.measured_at is not None and now - self.measured_at <= self.stale_seconds:
            return self.level, FLAG_MEASURED | (FLAG_DEGRADED if self.degraded else 0)
        return 0, FLAG_DEGRADED  # unknown usage is not throughput evidence

    def announce(self):
        state = self.wire()
        if state == self.announced:
            return None
        self.announced = state
        kind = 'measured' if state[1] & FLAG_MEASURED else ('unavailable' if state[1] & FLAG_DEGRADED else 'idle')
        return f'Density level {state[0]} ({kind}).'


class Sample:
    """Bounded roster and pseudonym-only wire bodies with monotonic freshness."""
    def __init__(self, key, tracker=None):
        self.key = key
        self.tracker = tracker or DensityTracker()
        self.body = None
        self.rows = None
        self.received = 0.0
        self.auth_expires = float('inf')

    def invalidate(self):
        self.body = None
        self.rows = None
        self.received = 0.0
        self.tracker.roster([])

    def update(self, rows):
        try:
            encode_sessions(rows, self.key)  # validate before the tracker sees rows
            self.tracker.roster(rows)
            level, flags = self.tracker.wire()
            self.body = encode_sessions(rows, self.key, level, flags)
        except ValueError:
            self.invalidate()
            return False
        self.rows = [{'id': row['id'], 'status': row['status'], 'provider': row.get('provider')} for row in rows]
        self.received = time.monotonic()
        counts = (len(rows), sum(1 for row in rows if row['status'] == 'working'))
        if counts != getattr(self, '_counts', None):
            self._counts = counts
            print(f'Sessions open={counts[0]} working={counts[1]}.', flush=True)
        return True

    def relevel(self):
        """Re-encode the current roster after a usage sweep changed the level."""
        if self.body is not None and self.rows is not None:
            level, flags = self.tracker.wire()
            self.body = encode_sessions(self.rows, self.key, level, flags)


_BOOT_TEXT = re.compile(r'boot=[0-9a-f]{8}( [a-z]{2,6}=[A-Za-z0-9_?-]{1,16}){0,12}')
_last_boot = [None]
# X-Board-Sleep (SPEC Contract B 4): sent on the first /v1/live request after a wake. Exactly these keys
# in this order; u32 counters, vbat in mV and pct are -1 when unknown.
_U32 = r'(0|[1-9][0-9]{0,9})'
_SIGNED = r'(-1|0|[1-9][0-9]{0,4})'
_SLEEP_TEXT = re.compile(
    r'slept_s=' + _U32 + r' cycles=' + _U32 + r' awake_ms=' + _U32 + r' wake=(pwr|boot|auto|usb|test)'
    r' vbus=[01] vbat0=' + _SIGNED + r' vbat1=' + _SIGNED + r' pct0=(-1|0|[1-9][0-9]{0,2})'
    r' pct1=(-1|0|[1-9][0-9]{0,2})')
_last_sleep = {}
MAX_SLEEP_BOARDS = 64


def _note_board_boot(values):
    """Print the board's reset reason + PMU power-off source once per board boot (authenticated
    requests only; strict charset, bounded). Lets a crash on battery be diagnosed without USB."""
    if len(values) != 1 or len(values[0]) > 160 or not _BOOT_TEXT.fullmatch(values[0]):
        return None
    if values[0] != _last_boot[0]:
        _last_boot[0] = values[0]
        print('Board boot: ' + values[0], flush=True)
    return values[0]


def _note_board_sleep(values, board_id):
    """Print one ``Board sleep: <board id> <value>`` line per distinct X-Board-Sleep value of a board
    (authenticated requests only). Anything that is not exactly the documented format is ignored."""
    if len(values) != 1 or len(values[0]) > 160 or not values[0].isascii():
        return None
    match = _SLEEP_TEXT.fullmatch(values[0])
    if match is None or any(int(match.group(i)) > 0xFFFFFFFF for i in (1, 2, 3)):
        return None
    board_id = str(board_id or '-')
    if _last_sleep.get(board_id) != values[0]:
        if board_id not in _last_sleep and len(_last_sleep) >= MAX_SLEEP_BOARDS:
            _last_sleep.pop(next(iter(_last_sleep)))
        _last_sleep[board_id] = values[0]
        print(f'Board sleep: {board_id} {values[0]}', flush=True)
    return values[0]


PHONE_ROUTES = {'/v1/pair/phone/start': 'POST', '/v1/pair/phone/status': 'GET', '/v1/pair/phone/forget': 'POST'}
PROVIDER_NAMES = ('hermes', 'home_assistant')


def phone_provider(request, phone):
    """Explicit configured provider selection, or None (404)."""
    values = request.headers.getall('X-Provider', [])
    if len(values) != 1:
        return None
    name = values[0]
    if name not in PROVIDER_NAMES or name not in getattr(phone, 'provider_names', ()):
        return None
    return name


PLUGINS = ('ai', 'home_assistant', 'sparkles')


def plugin_set(plugins):
    """Enabled board route groups resolved from configured provider tiles."""
    return frozenset(PLUGINS) if plugins is None else frozenset(plugins)


def make_app(sample, authorizer, allowed_ips=None, enroll=None, bots=None, phone=None,
             home=None, plugins=None):
    """Current board API. Every data route requires an enrolled identity and direct-peer checks.

    Phone routes require X-Provider; bots and Sensor apply provider-specific phone policy.
    Disabled tiles answer 404 before authentication or upstream work.
    """
    enabled = plugin_set(plugins)
    import ipaddress
    from .enroll import Authorizer
    if not isinstance(authorizer, Authorizer):
        raise TypeError('enrolled-board Authorizer required')
    if allowed_ips is not None:
        allowed_ips = frozenset(str(ipaddress.IPv4Address(ip)) for ip in allowed_ips)

    async def handle(request):
        headers = {'Cache-Control': 'no-store'}
        if allowed_ips is not None and request.remote not in allowed_ips:
            return web.Response(status=403, headers=headers)
        path = request.raw_path
        if path == '/v1/enroll' and enroll is not None:
            if request.method != 'POST':
                return web.Response(status=405, headers=headers)
            if request.content_length is None or request.content_length > 256:
                return web.Response(status=400, headers=headers)
            status, _ = enroll.submit(await request.read(), request.remote or '-')
            return web.Response(status=status, headers=headers)
        if (('ai' not in enabled and path == '/v1/bots')
                or ('home_assistant' not in enabled and path == '/v1/home')
                or ('sparkles' not in enabled and path == '/v1/live')):
            return web.Response(status=404, headers=headers)
        bots_route = bots is not None and path == '/v1/bots'
        home_route = home is not None and path == '/v1/home'
        phone_route = phone is not None and path in PHONE_ROUTES
        if path != '/v1/live' and not (bots_route or home_route or phone_route):
            return web.Response(status=404, headers=headers)
        if phone_route:
            if request.method != PHONE_ROUTES[path]:
                return web.Response(status=405, headers=headers)
            selected = phone_provider(request, phone)
            if selected is None:
                return web.Response(status=404, headers=headers)
        elif request.method != 'GET':
            return web.Response(status=405, headers=headers)
        values = request.headers.getall('Authorization', [])
        supplied = values[0].encode('utf-8') if len(values) == 1 else b''
        board = authorizer.board(supplied)
        if board is None:
            return web.Response(status=401, headers=headers)
        _note_board_boot(request.headers.getall('X-Board-Boot', []))
        _note_board_sleep(request.headers.getall('X-Board-Sleep', []), board.get('id'))
        if request.can_read_body:
            return web.Response(status=400, headers=headers)
        if phone_route:
            provider = selected
            if path.endswith('/start'):
                status, body = await phone.start(board, provider)
                if body is None:
                    return web.Response(status=status, headers=headers)
            elif path.endswith('/forget'):
                body = phone.forget(board, provider)
            else:
                body = phone.status(board, provider)
        elif home_route:
            if phone is None or not phone.home_allowed(board):
                return web.Response(status=409, text='home_auth', headers=headers)
            try:
                body = await home.frame()
            except Exception:
                return web.Response(status=503, headers=headers)
        elif bots_route:
            try:
                probe = getattr(bots, 'probe', None)
                if probe is not None and (probe.checked is None or not 0 <= probe.mono() - probe.checked < probe.ttl):
                    if probe.task is not None and not probe.task.done():
                        await probe.task
                    else:
                        await probe.refresh()
                body = bots.frame()
                if phone is None or not phone.allowed(board):
                    from .phone_pair import gated_bots_frame
                    body = gated_bots_frame(body)
            except Exception:
                return web.Response(status=503, headers=headers)
        else:
            if (sample.body is None or time.monotonic() - sample.received > 3.0
                    or time.time() >= sample.auth_expires):
                return web.Response(status=503, headers=headers)
            body = sample.body
        return web.Response(body=body, content_type='application/octet-stream', headers=headers)

    app = web.Application(client_max_size=1024)
    app.router.add_route('*', '/{tail:.*}', handle)
    return app


def encode_sessions(rows, key, level=0, flags=0):
    """WLS4: sorted (u64 pseudonym, u8 provider, u8 working bit) for every open session."""
    if (type(level) is not int or not 0 <= level < LEVELS
            or type(flags) is not int or flags & ~(FLAG_MEASURED | FLAG_DEGRADED)):
        raise ValueError('invalid level')
    if not isinstance(key, bytes) or len(key) != 32 or type(rows) is not list or len(rows) > 128:
        raise ValueError('invalid sample')
    seen = set()
    for row in rows:
        if type(row) is not dict:
            raise ValueError('invalid sample')
        sid, status, provider = row.get('id'), row.get('status'), row.get('provider')
        if (type(sid) is not str or not 1 <= len(sid) <= 256 or not sid.isascii()
                or not all(c.isalnum() or c in '-_.' for c in sid)
                or sid in seen or status not in ('working', 'idle', 'waiting', 'starting')
                or (provider is not None and type(provider) is not str)):
            raise ValueError('invalid sample')
        seen.add(sid)
    # Stable HMAC domain label for session pseudonyms.
    records = sorted((int.from_bytes(hmac.new(key, b'WLS1\0' + row['id'].encode(),
                                              hashlib.sha256).digest()[:8], 'little'),
                      PROVIDER_CODES.get(row.get('provider'), 0), int(row['status'] == 'working'))
                     for row in rows)
    ids = [r[0] for r in records]
    if len(set(ids)) != len(ids) or 0 in ids:
        raise ValueError('invalid sample')
    if not any(r[2] for r in records):
        level, flags = 0, flags & FLAG_DEGRADED
    return b'WLS4' + struct.pack('<HBB', len(records), level, flags) + b''.join(
        struct.pack('<QBB', *r) for r in records)


def load_board_key(key_file):
    from .common import read_private
    key = read_private(key_file, 128).decode('ascii').strip()
    if len(key) != 64 or any(c not in '0123456789abcdef' for c in key):
        raise ValueError('board.key must be 32-byte lowercase hex')
    return bytes.fromhex(key)


class Runtime:
    def __init__(self, runner, client, task, port, quota=None, bots=None, phone=None):
        self.runner, self.client, self.task, self.port = runner, client, task, port
        self.quota, self.bots, self.phone = quota, bots, phone

    async def close(self):
        import contextlib
        if self.bots is not None:
            await self.bots.close()
        if self.quota is not None:
            await self.quota.close()
        if self.phone is not None:
            await self.phone.close()
        self.task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.task
        await self.runner.cleanup()
        await self.client.close()


async def start_bridge(args):
    """Start the TLS API from the provider configuration resolved by cli.start."""
    import ssl
    from .common import read_private, new_client
    from .authelia_client import load_client_file, ClientCredentials
    from . import bots as bots_mod, phone_pair, quota as quota_mod, home_sensors
    key = load_board_key(args.board_key_file)
    client_config = load_client_file(args.authelia_client_file) if args.authelia_client_file else None
    read_private(args.key, 16384)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    tracker = DensityTracker(args.level_thresholds, args.ema_seconds)
    sample = Sample(key, tracker)
    client = new_client()
    plugins = plugin_set(args.plugins)
    quota_source = bot_source = phone = None
    runner = None
    try:
        if 'ai' in plugins:
            if args.quota_cache:
                quota_source = quota_mod.QuotaSource(args.quota_cache)
            probe = None
            if client_config and client_config.get('command_scope'):
                credentials = ClientCredentials({**client_config, 'scope': client_config['command_scope']}, client)
                probe = bots_mod.PluginProbe(bots_mod.CapabilityClient(args.gateway, credentials, client))
            bot_source = bots_mod.BotSource(args.quota_cache, args.profiles_dir, bots=args.bots,
                    providers=args.bot_providers, names=args.bot_names, probe=probe, quota_source=quota_source,
                    gate_path=pathlib.Path(args.key).parent / 'quota-blocks.json')
            bot_source.snapshot()
        if args.sign_ins:
            phone = phone_pair.PhonePairing.from_specs(args.registry, client, args.sign_ins)
        home = None
        if ('home_assistant' in plugins and args.home_client_file and phone is not None
                and phone.provider('home_assistant') is not None and pathlib.Path(args.home_client_file).exists()):
            home_config, resource = home_sensors.load_home_client(args.home_client_file)
            home = home_sensors.HomeSource(resource, ClientCredentials(home_config, client), client)
        runner = web.AppRunner(make_app(sample, args.authorizer, args.allow_ip, enroll=args.enroll,
                    bots=bot_source, phone=phone, home=home, plugins=plugins), access_log=None,
                    max_line_size=2048, max_field_size=2048, keepalive_timeout=5, shutdown_timeout=3)
        await runner.setup()
        site = web.TCPSite(runner, args.bind, args.port, ssl_context=context, backlog=16)
        await site.start()
        gateway = (UsageGateway(args.gateway, ClientCredentials(client_config, client), sample, client,
                                args.usage_interval)
                   if 'sparkles' in plugins and client_config else None)
        task = asyncio.create_task(gateway.run() if gateway is not None else asyncio.Event().wait())
        return Runtime(runner, client, task, site._server.sockets[0].getsockname()[1],
                       quota_source, bot_source, phone)
    except BaseException:
        if bot_source is not None:
            await bot_source.close()
        if quota_source is not None:
            await quota_source.close()
        if phone is not None:
            await phone.close()
        if runner is not None:
            await runner.cleanup()
        await client.close()
        raise
