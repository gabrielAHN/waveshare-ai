"""Sensor tile: GET /v1/home -> WHS1 frame for the board (air-quality readings from Home Assistant).

Everything site-specific is configuration (``home-client.json`` in the bridge config dir); no
hostname, path or provider is built in. Auth, end to end:
  * the board shows the Sensor tile only after a person signed it in with the phone QR (OAuth 2.0
    device grant at your sign-in provider, ``phone_pair``) and that person is in one of
    ``home_groups`` (bridge.json, default ``admins``). Otherwise /v1/home answers 409 ``home_auth``
    and the tile is disabled on the board.
  * the bridge then reads ``GET <resource_url>`` (e.g. ``https://ha.example.com/api/waveshare-sensors``)
    through YOUR auth proxy (forward-auth: Traefik/nginx/Caddy + Authelia, oauth2-proxy, ...) with an
    OAuth2 bearer token of a confidential client_credentials client (``scope`` defaults to
    Authelia's ``authelia.bearer.authz``; set your provider's scope(s) otherwise; ``audience`` = that
    URL). The proxy checks the token and lets only that client reach that one path. Whatever sits
    behind it (e.g. an nginx location that asks Home Assistant's ``/api/template`` with its own
    long-lived token) keeps the Home Assistant token: the bridge holds only its 0600 client file and
    short-lived access tokens in memory, the board holds nothing.

The endpoint's JSON contract (docs/SETUP.md "Sensor endpoint"):
  {"now": <epoch seconds, int>,
   "sensors": {"temperature"|"humidity"|"pressure"|"pm1"|"pm25"|"pm10":
               {"state": "<number as text, or 'unavailable'/'unknown'>", "at": <epoch seconds>}}}
  Units: degC, %RH, hPa, ug/m3. Missing keys or a non-numeric state show "--" on the board.

WHS1 (little-endian, FRAME_SIZE = 48):
  'WHS1' u8 version(1) u8 state u8 count(6) u8 0      state: 0 ok, 1 not set up, 2 unreachable, 3 refused
  6 x { i16 value_x10 (NONE16 unknown) u8 status(0 good,1 medium,2 bad,3 unknown) u8 0
        u16 age_s (NONE16 unknown, capped) }             order: temp C, humidity %, pressure hPa, PM1, PM2.5, PM10
  u32 crc32(all previous bytes)
"""
from __future__ import annotations

import asyncio
import re
import struct
import time
import zlib

import aiohttp

MAGIC, VERSION = b'WHS1', 1
KEYS = ('temperature', 'humidity', 'pressure', 'pm1', 'pm25', 'pm10')
NONE16 = 0xFFFF
FRAME_SIZE = 8 + len(KEYS) * 6 + 4
ST_OK, ST_SETUP, ST_UNREACHABLE, ST_REFUSED = 0, 1, 2, 3
GOOD, MEDIUM, BAD, UNKNOWN = 0, 1, 2, 3
CACHE_S = 5.0
STALE_AFTER_S = 60    # a reading older than this (when an age is known) shows as "--"


def _band(value, good, medium):
    """Inside ``good`` (lo, hi) -> GOOD, inside ``medium`` -> MEDIUM, else BAD."""
    if good[0] <= value <= good[1]:
        return GOOD
    if medium[0] <= value <= medium[1]:
        return MEDIUM
    return BAD


def status(key, value):
    """Comfort / air-quality bands per reading (pm1 uses the pm25 table; PM2.5 and PM10 follow the
    US EPA AQI breakpoints for good / moderate)."""
    if value is None:
        return UNKNOWN
    if key == 'temperature':
        return _band(value, (18, 26), (15, 30))
    if key == 'humidity':
        return _band(value, (30, 60), (20, 70))
    if key == 'pressure':
        return _band(value, (1000, 1025), (985, 1035))
    if key in ('pm1', 'pm25'):
        return GOOD if value <= 9.0 else (MEDIUM if value <= 35.4 else BAD)
    return GOOD if value <= 54 else (MEDIUM if value <= 154 else BAD)   # pm10


def encode_frame(state, sensors):
    out = bytearray(struct.pack('<4sBBBB', MAGIC, VERSION, state, len(KEYS), 0))
    for key in KEYS:
        item = sensors.get(key) if state == ST_OK else None
        value = item.get('value') if isinstance(item, dict) else None
        age = item.get('age_s') if isinstance(item, dict) else None
        if not isinstance(value, (int, float)) or isinstance(value, bool) or not -3276 <= value <= 3276:
            value = None
        if age is not None and (type(age) is not int or age < 0):
            age = None
        if value is not None and age is not None and age > STALE_AFTER_S:
            value = None          # an old reading is not a current one
        raw = NONE16 if value is None else int(round(value * 10)) & 0xFFFF
        out += struct.pack('<HBBH', raw, status(key, value), 0, NONE16 if age is None else min(age, NONE16 - 1))
    out += struct.pack('<I', zlib.crc32(out))
    assert len(out) == FRAME_SIZE
    return bytes(out)


def decode_frame(data):
    """Round-trip helper for tests: (state, {key: (value|None, status, age|None)})."""
    if len(data) != FRAME_SIZE or zlib.crc32(data[:-4]) != struct.unpack('<I', data[-4:])[0]:
        raise ValueError('bad frame')
    magic, version, state, count, _ = struct.unpack('<4sBBBB', data[:8])
    if magic != MAGIC or version != VERSION or count != len(KEYS):
        raise ValueError('bad frame')
    out = {}
    for i, key in enumerate(KEYS):
        raw, st, _, age = struct.unpack('<HBBH', data[8 + i * 6:14 + i * 6])
        value = None if raw == NONE16 else struct.unpack('<h', struct.pack('<H', raw))[0] / 10
        out[key] = (value, st, None if age == NONE16 else age)
    return state, out


HOME_REFUSAL = 'home_auth'
_RESOURCE_FIELDS = ('resource_url', 'resource_host_header')
_PATH = re.compile(r"/[A-Za-z0-9._~!$&'()*+,;=:@%/-]{1,255}")


def _resource(raw):
    """``resource_url``: https://HOST/PATH (or http://127.0.0.1[:port]/PATH on the proxy's own host),
    no credentials, query or fragment. ``resource_host_header``: optional bare host[:port] sent as
    Host (with X-Forwarded-Proto: https) when the URL points at 127.0.0.1."""
    from urllib.parse import urlsplit
    resource = {k: raw.get(k, '') for k in _RESOURCE_FIELDS}
    url = urlsplit(resource['resource_url'] if type(resource['resource_url']) is str else '')
    if (url.scheme not in ('http', 'https') or not url.hostname
            or (url.scheme == 'http' and url.hostname != '127.0.0.1')
            or url.username or url.password or url.query or url.fragment or not _PATH.fullmatch(url.path or '')):
        raise ValueError('resource_url must be https://HOST/PATH or http://127.0.0.1/PATH (no query/fragment)')
    host = resource['resource_host_header']
    if type(host) is not str or (host and not re.fullmatch(r'[A-Za-z0-9.-]{1,253}(:[0-9]{1,5})?', host)):
        raise ValueError('resource_host_header must be a bare host[:port]')
    return resource


def load_home_client(path):
    """The Sensor client file ``home-client.json`` (0600): the usual OAuth2 client fields
    (``token_endpoint``, ``client_id``, ``client_secret``, ``audience``, optional ``scope``,
    ``token_host_header``, ``ca_file``) + ``resource_url`` and ``resource_host_header``.
    Returns (client config for ClientCredentials, resource)."""
    import json as _json
    from .authelia_client import validate_client
    from .common import read_private
    raw = _json.loads(read_private(path, 8192))
    if type(raw) is not dict:
        raise ValueError('home client file must be an object')
    resource = _resource(raw)
    for key in _RESOURCE_FIELDS:
        raw.pop(key, None)
    return validate_client(raw, scope_rule='bearer'), resource


def _readings(body):
    """{'now': epoch, 'sensors': {key: {'state': str, 'at': epoch}}} -> encode_frame sensors dict."""
    now, sensors = body.get('now'), body.get('sensors')
    if type(now) is not int or type(sensors) is not dict:
        return None
    out = {}
    for key in KEYS:
        item = sensors.get(key)
        value = None
        if type(item) is dict and type(item.get('state')) is str:
            try:
                value = float(item['state'])
            except ValueError:
                value = None
        if value is None:
            out[key] = None
            continue
        # Freshness comes from Home Assistant, not from 'at': give MQTT sensors an expire_after
        # so HA turns a silent sensor into 'unavailable' (-> None above). 'at' is last_updated,
        # which only moves when the value CHANGES; a steady reading (PM1 = 0 for hours) is
        # current, and gating on it would blank rows the sensor still reports.
        out[key] = {'value': value, 'age_s': None}
    return out


class HomeSource:
    """Cached read through the auth proxy. ``credentials``: ClientCredentials of the bearer client."""

    def __init__(self, resource, credentials, client, mono=time.monotonic, log=None):
        self.url = resource['resource_url']
        self.host = resource.get('resource_host_header') or ''
        self.credentials, self.client, self.mono = credentials, client, mono
        self.log = log or (lambda message: print(message, flush=True))
        self.cached, self.cached_at = None, float('-inf')
        self.lock = asyncio.Lock()
        self.announced = None

    async def _get(self):
        from .authelia_client import ClientRejected
        from .common import Unavailable, parse_json
        for attempt in (0, 1):
            try:
                token = await self.credentials.token()
            except ClientRejected:
                return ST_REFUSED, {}
            except Unavailable:
                return ST_UNREACHABLE, {}
            headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/json'}
            token = None
            if self.host:
                headers['Host'] = self.host
                headers['X-Forwarded-Proto'] = 'https'
            try:
                async with self.client.get(self.url, allow_redirects=False, headers=headers,
                                           timeout=aiohttp.ClientTimeout(total=6)) as response:
                    if response.status in (401, 403):
                        if attempt == 0:
                            self.credentials.invalidate()
                            continue
                        return ST_REFUSED, {}
                    if response.status != 200:
                        return ST_UNREACHABLE, {}
                    data = await response.content.read(65537)
            except (aiohttp.ClientError, asyncio.TimeoutError, OSError):
                return ST_UNREACHABLE, {}
            finally:
                headers = None
            if len(data) > 65536:
                return ST_UNREACHABLE, {}
            try:
                body = parse_json(data)
            except ValueError:
                return ST_UNREACHABLE, {}
            readings = _readings(body) if type(body) is dict else None
            return (ST_OK, readings) if readings is not None else (ST_UNREACHABLE, {})
        return ST_REFUSED, {}

    async def frame(self):
        async with self.lock:
            if self.cached is not None and 0 <= self.mono() - self.cached_at < CACHE_S:
                return self.cached
            state, sensors = await self._get()
            self.cached, self.cached_at = encode_frame(state, sensors), self.mono()
            summary = 'ok' if state == ST_OK else ('refused by the auth proxy' if state == ST_REFUSED else 'unreachable')
            if summary != self.announced:
                self.announced = summary
                self.log(f'Home sensors: {summary}.')
            return self.cached
