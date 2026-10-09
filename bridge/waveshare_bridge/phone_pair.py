"""Provider-specific phone authorization via the OAuth device grant.

Providers sharing one gateway share one QR and approval; each checks its own groups.
Hermes approval uses phone_user/phone_name/phone_at/phone_groups in boards.json.
Other gateways use sign_ins. Tokens never enter the registry or board wire frame.
WPH1 flags: 1 required, 8 shared. Enrolled boards can read Sparkles; bots and
Gadget SDK access apply Hermes authorization and Sensor applies Home authorization.
"""
import asyncio
import hashlib
import json
import math
import re
import struct
import time
import zlib
from urllib.parse import urlsplit

import aiohttp

MAGIC = b'WPH1'
VERSION = 1
CODE_LEN, NAME_LEN, URI_LEN = 16, 33, 163
HEADER = '<4sBBBBHH'
FRAME_SIZE = struct.calcsize(HEADER) + CODE_LEN + NAME_LEN + URI_LEN + 4
STATES = {'none': 0, 'pending': 1, 'authorized': 2, 'denied': 3, 'expired': 4, 'refused': 5, 'error': 6}
STATE_NAMES = {v: k for k, v in STATES.items()}
FLAG_REQUIRED = 1        # this board may not use the provider until signed in
FLAG_SHARED = 8          # this provider's sign-in is shared with another provider (one QR signs into both)
FLAGS_KNOWN = 9
PROVIDERS = ('hermes', 'home_assistant')
DEVICE_GRANT = 'urn:ietf:params:oauth:grant-type:device_code'
SCOPE = 'openid profile groups'
DEFAULT_CLIENT_ID = 'waveshare-pairing'
DEFAULT_GROUPS = ('admins', 'hermes_users')
REFUSAL = 'phone_auth'
MAX_RESPONSE = 65536
RESULT_TTL = 600.0       # a terminal result (denied/expired/...) is reported this long
_CLIENT_ID = re.compile(r'[A-Za-z0-9._-]{1,64}')
# Endpoint paths on the issuer host (scheme://host of the client file's token_endpoint, or
# ``sign_in.issuer``). Authelia's by default; another provider sets ``sign_in.oidc_paths``, e.g. Keycloak
# {"device_authorization": "/realms/home/protocol/openid-connect/auth/device",
#  "token": "/realms/home/protocol/openid-connect/token",
#  "userinfo": "/realms/home/protocol/openid-connect/userinfo"}.
DEFAULT_OIDC_PATHS = {'device_authorization': '/api/oidc/device-authorization', 'token': '/api/oidc/token',
                      'userinfo': '/api/oidc/userinfo'}
_OIDC_PATH = re.compile(r"/[A-Za-z0-9._~!$&'()*+,;=:@%/-]{0,200}")
_GROUP = re.compile(r'[A-Za-z0-9._@-]{1,64}')
_USER_CODE = re.compile(r'[A-Za-z0-9-]{4,15}')
_DEVICE_CODE = re.compile(r'[A-Za-z0-9._~+/=-]{8,1024}')
_TOKEN = re.compile(r'[A-Za-z0-9._~+/=-]{16,8192}')


def _field(text, size):
    raw = (text or '').encode('ascii')
    if len(raw) >= size or any(not 0x20 <= c < 0x7f for c in raw):
        raise ValueError('field too long or not printable ASCII')
    return raw.ljust(size, b'\0')


def _printable(text, limit):
    return ''.join(ch for ch in str(text or '') if 0x20 <= ord(ch) < 0x7f).strip()[:limit]


def encode_status(state, expires_in=0, user_code='', name='', uri='', flags=0):
    if state not in STATES or type(flags) is not int or flags & ~FLAGS_KNOWN:
        raise ValueError('invalid phone status')
    expires_in = max(0, min(0xFFFF, int(expires_in)))
    out = struct.pack(HEADER, MAGIC, VERSION, STATES[state], flags, 0, expires_in, 0)
    out += _field(user_code, CODE_LEN) + _field(name, NAME_LEN) + _field(uri, URI_LEN)
    out += struct.pack('<I', zlib.crc32(out))
    assert len(out) == FRAME_SIZE
    return out


def decode_status(data):
    """Strict reference decoder for the current WPH1 frame."""
    if type(data) is not bytes or len(data) != FRAME_SIZE:
        raise ValueError('bad size')
    if struct.unpack('<I', data[-4:])[0] != zlib.crc32(data[:-4]):
        raise ValueError('bad crc')
    magic, version, state, flags, zero, expires_in, zero2 = struct.unpack_from(HEADER, data, 0)
    if magic != MAGIC or version != VERSION or state not in STATE_NAMES or flags & ~FLAGS_KNOWN or zero or zero2:
        raise ValueError('bad header')
    offset, fields = struct.calcsize(HEADER), []
    for size in (CODE_LEN, NAME_LEN, URI_LEN):
        raw = data[offset:offset + size]
        offset += size
        text = raw.split(b'\0', 1)[0]
        if b'\0' not in raw or any(not 0x20 <= c < 0x7f for c in text):
            raise ValueError('bad text')
        fields.append(text.decode('ascii'))
    return {'state': STATE_NAMES[state], 'flags': flags, 'expires_in': expires_in,
            'user_code': fields[0], 'name': fields[1], 'uri': fields[2]}


def gateway_key(issuer_base, client_id, paths, host_header=''):
    """Stable id of a sign-in gateway: first 16 hex of sha256 of
    ``issuer|client_id|device_authorization|token|userinfo|host_header`` (registry ``sign_ins`` key)."""
    paths = {**DEFAULT_OIDC_PATHS, **(paths or {})}
    raw = '|'.join((issuer_base.rstrip('/'), client_id, paths['device_authorization'], paths['token'],
                    paths['userinfo'], host_header or ''))
    return hashlib.sha256(raw.encode('utf-8')).hexdigest()[:16]


def oidc_paths(bridge_json):
    """``oidc_paths``: optional overrides of DEFAULT_OIDC_PATHS (absolute paths on the
    issuer host; never another host, a query or a dot segment)."""
    value = bridge_json.get('oidc_paths') or {}
    if not isinstance(value, dict) or set(value) - set(DEFAULT_OIDC_PATHS):
        raise ValueError('oidc_paths may only set device_authorization, token and userinfo')
    for path in value.values():
        if (not isinstance(path, str) or not _OIDC_PATH.fullmatch(path) or '//' in path
                or any(seg in ('.', '..') for seg in path.split('/'))):
            raise ValueError('oidc_paths values must be absolute paths on the issuer host')
    return {**DEFAULT_OIDC_PATHS, **value}


def settings(client, bridge_json):
    """Resolve the Hermes phone settings from provider-based bridge.json and its client."""
    from .config import _sign_in
    providers = bridge_json.get('providers', {})
    if not isinstance(providers, dict):
        raise ValueError('providers must be an object')
    sign_in = _sign_in((providers.get('hermes') or {}).get('sign_in'), 'hermes')
    endpoint = urlsplit(client['token_endpoint'])
    issuer = sign_in['issuer'] or f'{endpoint.scheme}://{endpoint.netloc}'
    host_header = sign_in['host_header']
    if host_header is None:
        host_header = '' if sign_in['issuer'] else client.get('token_host_header') or ''
    return {'issuer_base': issuer, 'host_header': host_header,
            'public_host': host_header.split(':')[0] if host_header else urlsplit(issuer).hostname,
            'ca_file': client.get('ca_file') or '', 'client_id': sign_in['client_id'],
            'groups': sign_in['groups'], 'require': sign_in['required'], 'paths': sign_in['paths']}


class _Flow:
    def __init__(self, device_code, user_code, uri, expires_in, interval, clock):
        self.device_code, self.user_code, self.uri = device_code, user_code, uri
        self.interval = interval
        self.started = clock()
        self.deadline = self.started + expires_in
        self.state = 'pending'
        self.name = ''
        self.finished = None
        self.task = None
        self.detail = ''


class _Gateway:
    """One sign-in gateway: its device flows (one per board) and where its sign-ins live in the registry
    (``flat`` = the Hermes provider's gateway: the flat phone_* fields; else ``sign_ins[key]``)."""

    def __init__(self, owner, spec, flat):
        self.owner = owner
        self.base = spec['issuer_base'].rstrip('/')
        self.paths = {**DEFAULT_OIDC_PATHS, **(spec.get('paths') or {})}
        self.client_id = spec.get('client_id') or DEFAULT_CLIENT_ID
        self.host_header = spec.get('host_header') or ''
        self.public_host = spec.get('public_host') or urlsplit(self.base).hostname
        self.key = spec.get('key') or gateway_key(self.base, self.client_id, self.paths, self.host_header)
        self.flat = flat
        self.providers = []
        self.flows = {}
        self._ssl = None
        if spec.get('ca_file'):
            import ssl
            self._ssl = ssl.create_default_context(cafile=spec['ca_file'])

    @property
    def label(self):
        return '' if self.flat else ' (%s)' % '+'.join(p.name for p in self.providers)

    def groups(self):
        out = set()
        for provider in self.providers:
            out |= provider.groups
        return out

    # -- registry --------------------------------------------------------------
    def record(self, board):
        """{'user', 'name', 'at', 'groups'} of the board's sign-in here, or None."""
        if board is None:
            return None
        if self.flat:
            user = board.get('phone_user')
            if not user:
                return None
            return {'user': user, 'name': board.get('phone_name') or user, 'at': board.get('phone_at') or 0,
                    'groups': board.get('phone_groups')}
        record = (board.get('sign_ins') or {}).get(self.key)
        return record if isinstance(record, dict) and record.get('user') else None

    def store(self, board_id, user, name, groups):
        registry = self.owner.registry
        if self.flat:
            return registry.set_phone(board_id, user, name, groups=groups)
        return registry.set_sign_in(board_id, self.key, user, name, groups=groups)

    def clear(self, board_id):
        registry = self.owner.registry
        return registry.clear_phone(board_id) if self.flat else registry.clear_sign_in(board_id, self.key)

    def cancel(self, board_id):
        flow = self.flows.get(board_id)
        if flow is not None and flow.task is not None and not flow.task.done():
            flow.task.cancel()

    # -- sign-in provider (OIDC) -------------------------------------------------
    def _headers(self, bearer=None):
        headers = {'Accept': 'application/json', 'User-Agent': 'waveshare-bridge/1'}
        if self.host_header:
            headers['Host'] = self.host_header
            headers['X-Forwarded-Proto'] = 'https'
        if bearer:
            headers['Authorization'] = 'Bearer ' + bearer
        return headers

    async def _call(self, method, path, form=None, bearer=None):
        async with self.owner.client.request(method, self.base + path, data=form, headers=self._headers(bearer),
                                             allow_redirects=False, ssl=self._ssl,
                                             timeout=aiohttp.ClientTimeout(total=8)) as response:
            data = bytearray()
            async for chunk in response.content.iter_chunked(8192):
                data.extend(chunk)
                if len(data) > MAX_RESPONSE:
                    raise ValueError('response too large')
            try:
                value = json.loads(data) if data else None
            except ValueError:
                value = None
            return response.status, value

    async def device_authorization(self):
        status, value = await self._call('POST', self.paths['device_authorization'],
                                         {'client_id': self.client_id, 'scope': SCOPE})
        if status != 200 or type(value) is not dict:
            raise ValueError('device authorization refused')
        device_code, user_code = value.get('device_code'), value.get('user_code')
        uri, expires_in, interval = value.get('verification_uri_complete'), value.get('expires_in'), value.get('interval', 5)
        if type(device_code) is not str or not _DEVICE_CODE.fullmatch(device_code):
            raise ValueError('bad device_code')
        if type(user_code) is not str or not _USER_CODE.fullmatch(user_code):
            raise ValueError('bad user_code')
        parts = urlsplit(uri) if type(uri) is str else None
        # The board renders this URL as a QR: it must be the configured portal over https, nothing else.
        if (parts is None or parts.scheme != 'https' or parts.hostname != self.public_host or parts.username
                or len(uri) >= URI_LEN or any(not 0x20 < ord(c) < 0x7f for c in uri)):
            raise ValueError('unexpected verification uri')
        if type(expires_in) is not int or not 30 <= expires_in <= 1800:
            raise ValueError('bad expires_in')
        if type(interval) is not int or not 1 <= interval <= 60:
            interval = 5
        return _Flow(device_code, user_code, uri, expires_in * self.owner.deadline_scale, interval, self.owner.clock)

    async def poll(self, board_id, flow):
        owner = self.owner
        server_errors = transport_errors = 0
        outcome, detail = 'expired', 'deadline'
        try:
            while True:
                await asyncio.sleep(flow.interval * owner.time_scale)
                if owner.clock() >= flow.deadline:
                    break
                try:
                    status, value = await self._call('POST', self.paths['token'], {
                        'grant_type': DEVICE_GRANT, 'device_code': flow.device_code, 'client_id': self.client_id})
                except (aiohttp.ClientError, asyncio.TimeoutError, OSError, ValueError) as exc:
                    transport_errors += 1
                    if transport_errors >= 5:
                        outcome, detail = 'error', 'transport ' + type(exc).__name__
                        break
                    continue
                transport_errors = 0
                error = value.get('error') if type(value) is dict else None
                detail = f"http {status} {_printable(error, 40) or '-'}"
                if status == 200:
                    outcome = await self._approved(board_id, flow, value)
                    detail = flow.detail or 'ok'
                    break
                if error == 'server_error' or status >= 500:
                    server_errors += 1          # Authelia 4.39: consent DENY -> 500 server_error
                    if server_errors >= 2:
                        outcome = 'denied'
                        break
                    continue
                server_errors = 0
                if status == 429:
                    # Authelia's per-IP token-endpoint rate limit (shared with the bridge's own
                    # client-credentials calls): not an answer - back off like slow_down.
                    flow.interval = min(flow.interval + 5, 60)
                    continue
                if error == 'authorization_pending':
                    continue
                if error == 'slow_down':
                    flow.interval = min(flow.interval + 5, 60)
                    continue
                outcome = {'access_denied': 'denied', 'expired_token': 'expired'}.get(error, 'error')
                break
        except asyncio.CancelledError:
            flow.state, flow.finished = 'none', owner.clock()
            raise
        flow.device_code = ''
        flow.state, flow.finished = outcome, owner.clock()
        # detail = HTTP status + OAuth error code / exception class only (no codes, no tokens).
        owner.log(f'Phone sign-in{self.label} {outcome} for board {board_id} ({detail}).', flush=True)

    async def _approved(self, board_id, flow, value):
        token = value.get('access_token') if type(value) is dict else None
        if type(token) is not str or not _TOKEN.fullmatch(token):
            flow.detail = 'bad access_token'
            return 'error'
        try:
            status, info = await self._call('GET', self.paths['userinfo'], bearer=token)
        except (aiohttp.ClientError, asyncio.TimeoutError, OSError, ValueError) as exc:
            flow.detail = 'userinfo ' + type(exc).__name__
            return 'error'
        finally:
            token = value = None
        if status != 200 or type(info) is not dict:
            flow.detail = f'userinfo http {status}'
            return 'error'
        groups = info.get('groups')
        if not isinstance(groups, list) or not self.groups().intersection(g for g in groups if isinstance(g, str)):
            flow.detail = 'no allowed group'
            return 'refused'
        user = _printable(info.get('preferred_username') or info.get('sub'), 64)
        if not user:
            return 'error'
        name = _printable(info.get('name') or user, 32)
        allowed_groups = sorted({g for g in groups if isinstance(g, str) and _GROUP.fullmatch(g)})[:16]
        if not self.store(board_id, user, name, allowed_groups):
            return 'error'   # the board was removed while the phone was approving
        flow.name = name
        return 'authorized'


class _Provider:
    def __init__(self, name, gateway, groups, require):
        self.name, self.gateway = name, gateway
        self.groups = frozenset(groups)
        self.require = bool(require)

    def authorized(self, board):
        """Signed in at this provider's gateway by a member of this provider's groups."""
        record = self.gateway.record(board)
        if record is None:
            return False
        groups = record.get('groups')
        return isinstance(groups, list) and bool(self.groups.intersection(groups))


class PhonePairing:
    """One device grant per enrolled board and sign-in gateway; per-provider group policy."""

    def __init__(self, registry, client, *, specs, clock=time.monotonic,
                 time_scale=1.0, log=print):
        self.registry, self.client = registry, client
        self.clock, self.time_scale, self.deadline_scale = clock, float(time_scale), 1.0
        self.min_restart = 5.0
        self.log = log
        self.gateways, self.providers = {}, {}
        flat_key = None
        for spec in specs:
            key = spec.get('key') or gateway_key(spec['issuer_base'], spec.get('client_id') or DEFAULT_CLIENT_ID,
                                                 spec.get('paths'), spec.get('host_header') or '')
            if spec['provider'] == 'hermes':
                flat_key = key
        for spec in specs:
            name = spec['provider']
            if name not in PROVIDERS or name in self.providers:
                raise ValueError('unknown or duplicate sign-in provider')
            key = spec.get('key') or gateway_key(spec['issuer_base'], spec.get('client_id') or DEFAULT_CLIENT_ID,
                                                 spec.get('paths'), spec.get('host_header') or '')
            gateway = self.gateways.get(key)
            if gateway is None:
                gateway = self.gateways[key] = _Gateway(self, dict(spec, key=key), flat=key == flat_key)
            provider = _Provider(name, gateway, spec.get('groups') or (), spec.get('require', True))
            gateway.providers.append(provider)
            self.providers[name] = provider

    @classmethod
    def from_specs(cls, registry, client, specs, **kwargs):
        return cls(registry, client, specs=list(specs), **kwargs)

    # -- configuration -------------------------------------------------------------
    @property
    def provider_names(self):
        return frozenset(self.providers)

    def provider(self, name):
        return self.providers.get(name)

    def shared(self, name):
        provider = self.providers.get(name)
        return provider is not None and len(provider.gateway.providers) > 1

    @property
    def require(self):
        hermes = self.providers.get('hermes')
        return hermes.require if hermes is not None else True

    @property
    def groups(self):
        hermes = self.providers.get('hermes')
        return hermes.groups if hermes is not None else frozenset()

    @property
    def home_groups(self):
        home = self.providers.get('home_assistant')
        return home.groups if home is not None else frozenset()

    # -- policy -------------------------------------------------------------
    def allowed(self, board):
        """May this authenticated board use Hermes bots and the gadget front?"""
        hermes = self.providers.get('hermes')
        if board is None or hermes is None:
            return False
        return not hermes.require or hermes.authorized(board)

    def home_allowed(self, board):
        """Sensor requires a sign-in by a member of the Home provider's groups."""
        home = self.providers.get('home_assistant')
        return board is not None and home is not None and home.authorized(board)

    def flags(self, board, provider='hermes'):
        p = self.providers[provider]
        return (FLAG_REQUIRED if p.require else 0) | (FLAG_SHARED if self.shared(provider) else 0)

    def flows_debug(self, board_id, provider='hermes'):
        flow = self.providers[provider].gateway.flows.get(board_id)
        return {'interval': flow.interval, 'state': flow.state} if flow else None

    # -- board routes --------------------------------------------------------
    def status(self, board, provider='hermes'):
        p = self.providers[provider]
        gateway = p.gateway
        flags = self.flags(board, provider)
        record = gateway.record(board)
        if p.authorized(board):
            return encode_status('authorized', 0, '', _printable(record['name'] or record['user'], 32), '', flags)
        flow = gateway.flows.get(board['id'])
        now = self.clock()
        if flow is not None and flow.state == 'pending':
            left = max(0, math.ceil(flow.deadline - now))
            return encode_status('pending', left, flow.user_code, '', flow.uri, flags)
        if record is not None:
            return encode_status('refused', 0, '', '', '', flags)   # signed in, but not in this provider's groups
        # A remembered 'authorized' result is only reported while the registry still says so (a CLI
        # revoke must take effect at once); denied/expired/refused/error are shown for RESULT_TTL.
        if (flow is not None and flow.finished is not None and now - flow.finished < RESULT_TTL
                and flow.state not in ('authorized', 'none')):
            return encode_status(flow.state, 0, '', '', '', flags)
        return encode_status('none', 0, '', '', '', flags)

    async def start(self, board, provider='hermes'):
        """(http_status, frame-or-None)."""
        p = self.providers[provider]
        gateway = p.gateway
        if p.authorized(board):
            # Already signed in: a new flow would report 'pending' and block the board's mic until the
            # code expired, although the registry still authorizes it. Sign out first to switch users.
            return 200, self.status(board, provider)
        now = self.clock()
        old = gateway.flows.get(board['id'])
        if old is not None and now - old.started < self.min_restart:
            return 429, None
        gateway.cancel(board['id'])
        try:
            flow = await gateway.device_authorization()
        except (aiohttp.ClientError, asyncio.TimeoutError, OSError, ValueError):
            self.log(f'Phone sign-in{gateway.label} unavailable (the sign-in provider refused the pairing client '
                     'or is unreachable).', flush=True)
            return 503, None
        gateway.flows[board['id']] = flow
        flow.task = asyncio.ensure_future(gateway.poll(board['id'], flow))
        self.log(f"Phone sign-in{gateway.label} started for board {board['id']} "
                 f"(code valid {int(flow.deadline - now)} s).", flush=True)
        return 200, self.status(board, provider)

    def forget(self, board, provider='hermes'):
        """Sign out of this provider's gateway: every provider sharing it is signed out too."""
        gateway = self.providers[provider].gateway
        gateway.cancel(board['id'])
        gateway.flows.pop(board['id'], None)
        if board.get('id'):
            gateway.clear(board['id'])
        self.log(f"Phone sign-in{gateway.label} cleared for board {board['id']}.", flush=True)
        fresh = self.registry.match_hash(board['token_sha256']) if 'token_sha256' in board else None
        return self.status(fresh or board, provider)

    async def close(self):
        tasks = []
        for gateway in self.gateways.values():
            for board_id in list(gateway.flows):
                gateway.cancel(board_id)
            tasks += [f.task for f in gateway.flows.values() if f.task is not None]
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)


def gated_bots_frame(frame):
    """A /v1/bots frame with every bot unavailable for reason ``phone_auth`` (board not signed in)."""
    from . import bots as bots_mod
    value = bots_mod.decode_frame(frame)
    for bot in value['bots']:
        bot['available'], bot['reason'], bot['reset_in_s'] = False, REFUSAL, None
    return bots_mod.encode_frame(value['bots'], value['flags'])
