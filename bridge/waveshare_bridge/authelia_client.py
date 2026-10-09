"""OAuth2 client_credentials machine credential for the bridge (any OIDC provider; tested with Authelia).

The bridge authenticates to the Hermes ``waveshare-sessions`` plugin route with a short-lived
access token minted by your sign-in provider for a confidential client (``waveshare-bridge`` in
plugins/hermes/README.md). The same code mints the bearer tokens the bridge sends through your auth proxy
(forward-auth) to other protected services, e.g. the Sensor tile's Home Assistant endpoint
(``home_sensors.py``, ``scope_rule='bearer'``). There is no browser login, no refresh token and no
user session: when the token nears expiry the bridge just asks the token endpoint for a new one
with its client secret.

The client secret lives only in a 0600 JSON file on the Hermes host (``authelia-client.json`` /
``home-client.json`` in the bridge config dir; client files work with any
provider); it is never logged, printed, sent to the board, or included in exception text/repr.
"""
import base64
import json
import math
import re
import time
from urllib.parse import quote, urlsplit

import aiohttp
from .common import AuthUnavailable, Unavailable, read_private

REFRESH_MARGIN = 60.0        # fetch a new token this many seconds before expiry
MIN_BACKOFF = 5.0
MAX_BACKOFF = 300.0
MAX_RESPONSE = 65536
_FIELDS = {'token_endpoint', 'client_id', 'client_secret', 'scope', 'audience', 'token_host_header', 'ca_file',
           'command_scope'}
_TOKEN_CHARS = re.compile(r'[A-Za-z0-9._~+/=-]{16,8192}')
_HOST = re.compile(r'[A-Za-z0-9.-]{1,253}(:[0-9]{1,5})?')


class ClientRejected(AuthUnavailable):
    """The token endpoint refused the client (unknown client / bad secret / scope not allowed)."""


def _endpoint(url):
    parts = urlsplit(url)
    if parts.username or parts.password or parts.query or parts.fragment or not parts.hostname:
        raise ValueError('token_endpoint must be a plain absolute URL')
    if parts.scheme == 'https' or (parts.scheme == 'http' and parts.hostname == '127.0.0.1'):
        return url
    raise ValueError('token_endpoint must be https:// or http://127.0.0.1')


def load_client_file(path):
    """Validated client config from a private (0600, owned, non-symlink) JSON file."""
    try:
        value = json.loads(read_private(path, 8192))
    except json.JSONDecodeError:
        raise ValueError('client file is not JSON') from None
    return validate_client(value)


BEARER_SCOPE = 'authelia.bearer.authz'   # Authelia's forward-auth bearer scope: the default for bearer clients
_SCOPE_TOKEN = re.compile(r'[A-Za-z0-9._:/-]{1,128}')
_RESERVED_SCOPES = ('openid', 'offline_access')


def bearer_scope(value):
    """The scope(s) of a bearer client (scope_rule='bearer'): missing/empty = Authelia's
    ``authelia.bearer.authz``; otherwise 1..8 space-separated scope tokens named by your own provider
    (e.g. ``ha.sensors.read``), never ``openid``/``offline_access`` (no ID or refresh tokens) and
    never a ``hermes.*`` scope of the Hermes plugin client."""
    if value is None or value == '':
        return BEARER_SCOPE
    if type(value) is not str:
        raise ValueError('scope must be a string of space-separated scopes')
    tokens = value.split(' ')
    if (not 1 <= len(tokens) <= 8 or not all(_SCOPE_TOKEN.fullmatch(t) for t in tokens)
            or any(t in _RESERVED_SCOPES for t in tokens) or len(set(tokens)) != len(tokens)):
        raise ValueError('a bearer client scope must be 1-8 space-separated scopes (not openid/offline_access)')
    if any(t.startswith('hermes.') for t in tokens):
        # A Hermes plugin scope here would mint tokens the Hermes plugin accepts and hand them to a
        # different service: keep the Hermes client and the bearer client strictly separate.
        raise ValueError('a bearer client must not ask for a hermes.* scope')
    return value


def validate_client(value, scope_rule='custom'):
    """Validate a client config dict.

    ``scope_rule='custom'`` (the Hermes plugin client): ``scope`` is one dedicated custom scope and
    ``command_scope`` an optional bot-capability scope. ``scope_rule='bearer'`` (a client whose tokens an auth
    proxy checks, such as the Sensor tile's ``home-client.json``): ``scope`` defaults to Authelia's
    ``authelia.bearer.authz`` and may instead name the scope(s) your provider/proxy expects; such a
    client never has a command scope."""
    if type(value) is not dict or set(value) - _FIELDS:
        raise ValueError('client file has unexpected fields')
    if scope_rule == 'bearer':
        value = {**value, 'scope': bearer_scope(value.get('scope'))}
    for name in ('token_endpoint', 'client_id', 'client_secret', 'scope', 'audience'):
        if type(value.get(name)) is not str or not value[name].strip():
            raise ValueError(f'client file missing {name}')
    _endpoint(value['token_endpoint'])
    secret = value['client_secret']
    if len(secret) < 32 or len(secret) > 512 or not all(33 <= ord(c) <= 126 for c in secret):
        raise ValueError('client secret must be >= 32 printable characters')
    scope = value['scope']
    if scope_rule == 'bearer':
        if value.get('command_scope'):
            raise ValueError('a bearer client has no command_scope')
    elif scope in ('openid', 'offline_access', BEARER_SCOPE) or not re.fullmatch(r'[A-Za-z0-9._:-]{1,128}', scope):
        raise ValueError('scope must be a dedicated custom scope')
    command_scope = value.get('command_scope', '')
    if type(command_scope) is not str or (command_scope and (
            command_scope in ('openid', 'offline_access', scope)
            or not re.fullmatch(r'[A-Za-z0-9._:-]{1,128}', command_scope))):
        raise ValueError('command_scope must be a separate dedicated custom scope')
    host = value.get('token_host_header', '')
    if type(host) is not str or (host and not _HOST.fullmatch(host)):
        raise ValueError('token_host_header must be a bare host[:port]')
    ca_file = value.get('ca_file', '')
    if type(ca_file) is not str:
        raise ValueError('ca_file must be a path string')
    return {k: value.get(k, '') for k in _FIELDS}


def parse_token_response(value, now):
    """(access_token, expires_at) from an RFC 6749 token response, or None if unacceptable."""
    if type(value) is not dict:
        return None
    token, kind, lifetime = value.get('access_token'), value.get('token_type'), value.get('expires_in')
    if type(token) is not str or not _TOKEN_CHARS.fullmatch(token):
        return None
    if type(kind) is not str or kind.lower() != 'bearer':
        return None
    if type(lifetime) not in (int, float) or not math.isfinite(lifetime) or not 1 <= lifetime <= 86400:
        return None
    if 'refresh_token' in value:
        return None  # client_credentials must never be issued a refresh token; refuse to keep one
    return token, now + float(lifetime)


class ClientCredentials:
    """In-memory access-token cache for one confidential client."""

    def __init__(self, config, client, clock=time.time):
        self._public = {k: config[k] for k in ('token_endpoint', 'client_id', 'scope', 'audience')}
        self._host = config.get('token_host_header', '')
        self._basic = 'Basic ' + base64.b64encode(
            (quote(config['client_id'], safe='') + ':' + quote(config['client_secret'], safe='')).encode()).decode()
        self._ssl = None
        if config.get('ca_file'):
            import ssl
            self._ssl = ssl.create_default_context(cafile=config['ca_file'])
        self.client, self.clock = client, clock
        self._token = None
        self.expires_at = 0.0
        self.retry_at = 0.0
        self.backoff = 0.0
        self.last_rejected = False

    def __repr__(self):
        return f'ClientCredentials(client_id={self._public["client_id"]!r}, scope={self._public["scope"]!r})'

    def invalidate(self):
        self._token = None
        self.expires_at = 0.0

    def _fail(self, rejected):
        self.invalidate()
        self.last_rejected = rejected
        self.backoff = MIN_BACKOFF if not self.backoff else min(self.backoff * 2, MAX_BACKOFF)
        self.retry_at = self.clock() + self.backoff
        return ClientRejected() if rejected else Unavailable()

    async def token(self):
        """A valid access token (cached until REFRESH_MARGIN before expiry). Raises ClientRejected
        when the token endpoint refuses the client, Unavailable when it cannot be reached; both back off
        5s, 10s, ... capped at MAX_BACKOFF, and no request is made inside the backoff window."""
        now = self.clock()
        if self._token is not None and now < self.expires_at - REFRESH_MARGIN:
            return self._token
        if now < self.retry_at:
            raise ClientRejected() if self.last_rejected else Unavailable()
        headers = {'Authorization': self._basic, 'Accept': 'application/json'}
        if self._host:
            headers['Host'] = self._host
        form = {'grant_type': 'client_credentials', 'scope': self._public['scope'],
                'audience': self._public['audience']}
        try:
            async with self.client.post(self._public['token_endpoint'], data=form, headers=headers,
                                        allow_redirects=False, ssl=self._ssl,
                                        timeout=aiohttp.ClientTimeout(total=5)) as response:
                status = response.status
                data = bytearray()
                async for chunk in response.content.iter_chunked(8192):
                    data.extend(chunk)
                    if len(data) > MAX_RESPONSE:
                        break
        except Exception:  # noqa: BLE001 — transport failure; never surface details (may echo headers)
            raise self._fail(False) from None
        if status in (400, 401, 403):
            raise self._fail(True)
        parsed = None
        if status == 200 and len(data) <= MAX_RESPONSE:
            try:
                parsed = parse_token_response(json.loads(data), self.clock())
            except ValueError:
                parsed = None
        if parsed is None:
            raise self._fail(False)
        self._token, self.expires_at = parsed
        self.backoff, self.retry_at, self.last_rejected = 0.0, 0.0, False
        return self._token
