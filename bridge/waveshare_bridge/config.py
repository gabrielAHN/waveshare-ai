"""Strict provider-based configuration for the Waveshare AI bridge.

The private config directory holds bridge.json, machine OAuth clients, TLS keys,
board.key, boards.json, gadget-grants.json, quota-blocks.json and control.sock.
Hermes serves Sparkles and Ask; Home Assistant serves Sensor. Identical provider
sign-in gateways share one phone QR while each provider checks its own groups.
"""
import ipaddress
import json
import os
import pathlib
import re
import stat
from urllib.parse import urlsplit

PRIVATE_NETS = ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')
DEFAULT_GATEWAY = 'http://127.0.0.1:9119'
HERMES_CLIENT_FILE = 'authelia-client.json'
HOME_CLIENT_FILE = 'home-client.json'
PROVIDERS = ('hermes', 'home_assistant')
HERMES_TILES = ('sparkles', 'ask')
PROVIDER_TILES = {'home_assistant': ('sensor',)}
DEFAULT_SIGN_IN_GROUPS = {'hermes': ('admins', 'hermes_users'), 'home_assistant': ('admins',)}
TOP_KEYS = ('bind', 'port', 'name', 'mdns', 'allow_ip')
HERMES_KEYS = ('gateway', 'client_file', 'sign_in', 'tiles', 'bots', 'bot_names', 'bot_providers', 'profiles_dir',
               'quota_cache', 'gadget_sdk', 'sparkles')
HOME_KEYS = ('client_file', 'sign_in')
SIGN_IN_KEYS = ('client_id', 'groups', 'required', 'issuer', 'oidc_paths', 'host_header', 'client_file')
_FILE_NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}')
_HOST = re.compile(r'[A-Za-z0-9.-]{1,253}(:[0-9]{1,5})?')


def default_dir():
    override = os.environ.get('WAVESHARE_AI_CONFIG', '').strip()
    return pathlib.Path(override).expanduser() if override else pathlib.Path.home() / '.config' / 'waveshare-ai'


def check_private(path, kind='file'):
    st = os.lstat(path)
    if stat.S_ISLNK(st.st_mode):
        raise ValueError(f'{path.name} must not be a symlink')
    if st.st_mode & 0o077:
        raise ValueError(f'{path.name} must be private (chmod {"700" if kind == "dir" else "600"})')


def rfc1918(ip):
    address = ipaddress.IPv4Address(ip)
    if not any(address in ipaddress.ip_network(n) for n in PRIVATE_NETS):
        raise ValueError('bind must be an RFC1918 LAN IPv4 address')
    return str(address)


def loopback_gateway(url):
    parts = urlsplit(url)
    if parts.scheme not in ('http', 'https') or parts.hostname not in ('127.0.0.1', 'localhost', '::1'):
        raise ValueError('gateway must be a loopback URL (the Hermes dashboard on this host)')
    return url.rstrip('/')


def gadget_gateway(data, key='gadget_sdk'):
    """The LAN front for the Hermes gateway's stock Gadget SDK platforms (gadget_front.py), or None.

    ``{"port": 8768, "profiles": {"helper": 8775, "atlas": 8776, "coding": 8777}}``: boards dial
    ``wss://<bind>:port/gadget/<bot>``; the front checks them and pipes to that profile's loopback
    gadget listener in the Hermes gateway (bridge.json ``providers.hermes.gadget_sdk``).
    """
    value = data.get(key)
    if value is None:
        return None
    value = _object(value, key, ('port', 'profiles'))
    port, profiles = value.get('port'), value.get('profiles')
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError(f'{key}.port must be 1..65535')
    if not isinstance(profiles, dict) or not profiles:
        raise ValueError(f'{key}.profiles must map bot profiles to loopback ports')
    from .bots import valid_bot
    out = {}
    for name, upstream in profiles.items():
        if not valid_bot(name):
            raise ValueError(f'{key}.profiles keys must be 1..11-character Hermes profile ids (not default)')
        if type(upstream) is not int or not 1 <= upstream <= 65535 or upstream == port:
            raise ValueError(f'{key}.profiles ports must be 1..65535')
        out[name] = upstream
    if len(set(out.values())) != len(out):
        raise ValueError(f'{key}.profiles ports must be distinct')
    return {'port': port, 'upstream_host': '127.0.0.1', 'profiles': out}


def default_quota_cache(environ=None):
    """The official Hermes ``quota`` plugin's cache on this host: ``$HERMES_HOME/quota_cache.json``
    (default ``~/.hermes``). Resolved locally from the environment; the gateway is never asked."""
    environ = os.environ if environ is None else environ
    home = (environ.get('HERMES_HOME') or '').strip()
    return (pathlib.Path(home).expanduser() if home else pathlib.Path.home() / '.hermes') / 'quota_cache.json'


def default_profiles_dir(environ=None):
    """Hermes profiles on this host (``$HERMES_HOME/profiles``, default ``~/.hermes/profiles``), read-only."""
    environ = os.environ if environ is None else environ
    home = (environ.get('HERMES_HOME') or '').strip()
    return (pathlib.Path(home).expanduser() if home else pathlib.Path.home() / '.hermes') / 'profiles'


PLUGINS = ('ai', 'home_assistant', 'sparkles')   # the route groups (config.served_routes)


def _bot_map(data, key):
    value = data.get(key) or {}
    if not isinstance(value, dict) or not all(isinstance(k, str) and isinstance(v, str) and len(v) <= 32
                                              for k, v in value.items()):
        raise ValueError(f'{key} must map bot id -> string')
    return dict(value)


# --------------------------------------------------------------------------- providers layout

def _object(value, path, keys):
    if value is None:
        value = {}
    if not isinstance(value, dict):
        raise ValueError(f'{path} must be an object')
    unknown = sorted(set(value) - set(keys))
    if unknown:
        raise ValueError(f'unknown key(s) in {path}: {", ".join(map(str, unknown))}')
    return value


def _file_name(value, path):
    if not isinstance(value, str) or not _FILE_NAME.fullmatch(value):
        raise ValueError(f'{path} must be a file name in the config directory')
    return value


def _issuer(value, path):
    """An issuer base URL: https://HOST[:PORT] (or http://127.0.0.1[:PORT]) without path or credentials."""
    if value is None:
        return None
    parts = urlsplit(value) if isinstance(value, str) else None
    try:
        port_ok = parts is not None and (parts.port is None or 1 <= parts.port <= 65535)
    except ValueError:
        port_ok = False
    if (parts is None or not port_ok or not parts.hostname or parts.username or parts.password or parts.query
            or parts.fragment or parts.path not in ('', '/')
            or not (parts.scheme == 'https' or (parts.scheme == 'http' and parts.hostname == '127.0.0.1'))):
        raise ValueError(f'{path} must be https://HOST[:PORT] (or http://127.0.0.1[:PORT]) without a path')
    return f'{parts.scheme}://{parts.netloc}'


def _sign_in(value, provider):
    """Normalized ``sign_in`` (every key present; defaults filled in; oidc_paths merged into ``paths``)."""
    from .phone_pair import DEFAULT_CLIENT_ID, _CLIENT_ID, _GROUP, oidc_paths
    path = f'providers.{provider}.sign_in'
    value = _object(value, path, SIGN_IN_KEYS)
    if provider != 'hermes' and 'required' in value:
        raise ValueError(f'{path}.required is not a setting: the {provider} provider always needs a sign-in')
    client_id = value.get('client_id', DEFAULT_CLIENT_ID)
    if not isinstance(client_id, str) or not _CLIENT_ID.fullmatch(client_id):
        raise ValueError(f'{path}.client_id must be a plain client id')
    groups = value.get('groups', list(DEFAULT_SIGN_IN_GROUPS[provider]))
    if (not isinstance(groups, list) or not 1 <= len(groups) <= 16
            or not all(isinstance(g, str) and _GROUP.fullmatch(g) for g in groups)):
        raise ValueError(f'{path}.groups must be a non-empty list of group names')
    required = value.get('required', True)
    if type(required) is not bool:
        raise ValueError(f'{path}.required must be true or false')
    host = value.get('host_header')
    if host is not None and (not isinstance(host, str) or (host and not _HOST.fullmatch(host))):
        raise ValueError(f'{path}.host_header must be a bare host[:port], "" or null')
    paths = oidc_paths({'oidc_paths': value.get('oidc_paths')})
    client_file = value.get('client_file')
    return {'client_id': client_id, 'groups': list(groups), 'required': required,
            'issuer': _issuer(value.get('issuer'), path + '.issuer'), 'paths': paths, 'host_header': host,
            'client_file': None if client_file is None else _file_name(client_file, path + '.client_file')}


def _tiles(value):
    if value is None:
        return list(HERMES_TILES)
    if (not isinstance(value, list) or not all(isinstance(t, str) for t in value)
            or len(set(value)) != len(value)):
        raise ValueError('providers.hermes.tiles must be a list of tile names')
    unknown = sorted(set(value) - set(HERMES_TILES))
    if unknown:
        raise ValueError('unknown Hermes tile(s): %s (known: %s)' % (', '.join(unknown), ', '.join(HERMES_TILES)))
    return [t for t in HERMES_TILES if t in value]


def _hermes(value):
    from .bots import valid_bot
    value = _object(value, 'providers.hermes', HERMES_KEYS)
    sparkles = _object(value.get('sparkles'), 'providers.hermes.sparkles',
                       ('level_thresholds', 'usage_interval', 'ema_seconds'))
    out = {
        'gateway': loopback_gateway(value.get('gateway', DEFAULT_GATEWAY)),
        'client_file': _file_name(value.get('client_file', HERMES_CLIENT_FILE), 'providers.hermes.client_file'),
        'sign_in': _sign_in(value.get('sign_in'), 'hermes'),
        'tiles': _tiles(value.get('tiles')),
        'bots': value.get('bots') or ['helper', 'atlas', 'coding'],
        'bot_names': _bot_map(value, 'bot_names'),
        'bot_providers': _bot_map(value, 'bot_providers'),
        'profiles_dir': value.get('profiles_dir') or None,
        'quota_cache': (False if value.get('quota_cache') is False else value.get('quota_cache') or None),
        'gadget_sdk': gadget_gateway(value, 'gadget_sdk'),
        'sparkles': {'level_thresholds': sparkles.get('level_thresholds'),
                     'usage_interval': float(sparkles.get('usage_interval', 5.0)),
                     'ema_seconds': float(sparkles.get('ema_seconds', 60.0))},
    }
    if not 2 <= out['sparkles']['usage_interval'] <= 60 or not 0.5 <= out['sparkles']['ema_seconds'] <= 600:
        raise ValueError('invalid usage interval or smoothing')
    bots = out['bots']
    if (not isinstance(bots, list) or not 1 <= len(bots) <= 3 or len(set(map(str, bots))) != len(bots)
            or not all(valid_bot(b) for b in bots)):
        raise ValueError('bots must be a list of 1..3 profile ids of 1..11 characters (not default)')
    return out


def _home_assistant(value):
    value = _object(value, 'providers.home_assistant', HOME_KEYS)
    return {'client_file': _file_name(value.get('client_file', HOME_CLIENT_FILE),
                                      'providers.home_assistant.client_file'),
            'sign_in': _sign_in(value.get('sign_in'), 'home_assistant')}


def parse_providers(value):
    """Normalized ``providers`` (only the configured ones, in PROVIDERS order)."""
    if not isinstance(value, dict):
        raise ValueError('providers must be an object')
    unknown = sorted(set(value) - set(PROVIDERS))
    if unknown:
        raise ValueError('unknown provider(s): %s (known: %s)' % (', '.join(map(str, unknown)), ', '.join(PROVIDERS)))
    out = {}
    if 'hermes' in value:
        out['hermes'] = _hermes(value['hermes'])
    if 'home_assistant' in value:
        out['home_assistant'] = _home_assistant(value['home_assistant'])
    return out


def provider_tiles(name, provider):
    return list(provider['tiles']) if name == 'hermes' else list(PROVIDER_TILES[name])


def served_routes(providers):
    """The board route groups (live_bridge PLUGINS) the configured providers serve."""
    hermes = providers.get('hermes')
    out = set()
    if hermes is not None and 'sparkles' in hermes['tiles']:
        out.add('sparkles')
    if hermes is not None and 'ask' in hermes['tiles']:
        out.add('ai')
    if 'home_assistant' in providers:
        out.add('home_assistant')
    return frozenset(out)


def parse(data, root):
    """bridge.json -> the effective configuration."""
    if not isinstance(data, dict):
        raise ValueError('bridge.json must be an object')
    if 'providers' not in data:
        raise ValueError('bridge.json has no providers object')
    unknown = sorted(k for k in data if k not in TOP_KEYS and k != 'providers')
    if unknown:
        raise ValueError('unknown bridge.json key(s): %s' % ', '.join(map(str, unknown)))
    raw = data
    port = raw.get('port', 8098)
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError('port must be 1..65535')
    allow = raw.get('allow_ip')
    if allow is not None:
        if not isinstance(allow, list) or not allow:
            raise ValueError('allow_ip must be a non-empty list or null')
        allow = [str(ipaddress.IPv4Address(ip)) for ip in allow]
    providers = parse_providers(raw['providers'])
    cfg = {
        'dir': root,
        'providers': providers,
        'plugins': served_routes(providers),
        'bind': rfc1918(raw.get('bind', '')),
        'port': port,
        'name': str(raw.get('name') or 'Waveshare AI bridge')[:32],
        'allow_ip': allow,
        'mdns': raw.get('mdns', True) is not False,
        'gadget_grants': root / 'gadget-grants.json',
        'cert': root / 'tls.crt',
        'key': root / 'tls.key',
        'board_key': root / 'board.key',
        'boards': root / 'boards.json',
        'control': root / 'control.sock',
    }
    cfg.update(_flat(providers, root))
    return cfg


def _flat(providers, root):
    """The flat per-provider keys the runtime reads (defaults when a provider is absent)."""
    from .phone_pair import DEFAULT_CLIENT_ID, DEFAULT_OIDC_PATHS
    hermes = providers.get('hermes')
    home = providers.get('home_assistant')
    out = {
        'gateway': DEFAULT_GATEWAY, 'gadget_gateway': None,
        'level_thresholds': None, 'usage_interval': 5.0, 'ema_seconds': 60.0, 'quota_cache': None,
        'bots': ['helper', 'atlas', 'coding'], 'bot_providers': {}, 'bot_names': {},
        'profiles_dir': default_profiles_dir(), 'client_file': None,
        'require_phone_auth': True, 'phone_groups': list(DEFAULT_SIGN_IN_GROUPS['hermes']),
        'phone_client_id': DEFAULT_CLIENT_ID, 'oidc_paths': dict(DEFAULT_OIDC_PATHS),
        'home_groups': list(DEFAULT_SIGN_IN_GROUPS['home_assistant']), 'home_client_file': None,
    }
    if hermes is not None:
        sign_in = hermes['sign_in']
        out.update({
            'gateway': hermes['gateway'], 'gadget_gateway': hermes['gadget_sdk'],
            'level_thresholds': hermes['sparkles']['level_thresholds'],
            'usage_interval': hermes['sparkles']['usage_interval'], 'ema_seconds': hermes['sparkles']['ema_seconds'],
            # Optional quota telemetry cache: a path or false to disable.
            'quota_cache': (None if hermes['quota_cache'] is False else
                            pathlib.Path(str(hermes['quota_cache'])).expanduser() if hermes['quota_cache']
                            else default_quota_cache()),
            'bots': hermes['bots'], 'bot_providers': hermes['bot_providers'], 'bot_names': hermes['bot_names'],
            'profiles_dir': (pathlib.Path(str(hermes['profiles_dir'])).expanduser() if hermes['profiles_dir']
                             else default_profiles_dir()),
            'client_file': root / hermes['client_file'],
            'require_phone_auth': sign_in['required'], 'phone_groups': sign_in['groups'],
            'phone_client_id': sign_in['client_id'], 'oidc_paths': sign_in['paths'],
        })
    if home is not None:
        out['home_groups'] = home['sign_in']['groups']
        out['home_client_file'] = root / home['client_file']
    return out


def load(directory=None):
    root = pathlib.Path(directory or default_dir())
    check_private(root, 'dir')
    path = root / 'bridge.json'
    check_private(path)
    return parse(json.loads(path.read_text()), root)


# --------------------------------------------------------------------------- sign-in gateways

def _sign_in_source(path):
    """token_endpoint / token_host_header / ca_file of a private client file (either client kind)."""
    from .authelia_client import _endpoint
    from .common import read_private
    try:
        raw = json.loads(read_private(path, 8192))
    except json.JSONDecodeError:
        raise ValueError('client file is not JSON') from None
    if not isinstance(raw, dict) or not isinstance(raw.get('token_endpoint'), str):
        raise ValueError('client file has no token_endpoint')
    _endpoint(raw['token_endpoint'])
    host = raw.get('token_host_header') or ''
    if not isinstance(host, str) or (host and not _HOST.fullmatch(host)):
        raise ValueError('token_host_header must be a bare host[:port]')
    ca_file = raw.get('ca_file') or ''
    if not isinstance(ca_file, str):
        raise ValueError('ca_file must be a path string')
    return {'token_endpoint': raw['token_endpoint'], 'token_host_header': host, 'ca_file': ca_file}


def sign_in_specs(cfg, source=None):
    """Resolve each configured provider's sign-in gateway.

    Returns ``(specs, problems)``: ``specs`` = one dict per provider whose gateway resolves
    ({provider, key, issuer_base, client_id, host_header, public_host, ca_file, paths, groups, require});
    ``problems`` = {provider: reason} (file names and exception classes only, never values).
    """
    from .phone_pair import gateway_key
    source = source or _sign_in_source
    specs, problems = [], {}
    for name, provider in cfg['providers'].items():
        sign_in = provider['sign_in']
        file_name = sign_in['client_file'] or provider['client_file']
        client = None
        try:
            client = source(cfg['dir'] / file_name)
        except (OSError, ValueError) as error:
            if sign_in['issuer'] is None:
                problems[name] = f'{file_name}: {type(error).__name__}'
                continue
        if sign_in['issuer'] is None:
            parts = urlsplit(client['token_endpoint'])
            issuer = f'{parts.scheme}://{parts.netloc}'
            host = sign_in['host_header'] if sign_in['host_header'] is not None else client['token_host_header']
        else:
            issuer, host = sign_in['issuer'], sign_in['host_header'] or ''
        specs.append({'provider': name, 'issuer_base': issuer, 'client_id': sign_in['client_id'],
                      'host_header': host, 'public_host': host.split(':')[0] if host else urlsplit(issuer).hostname,
                      'ca_file': (client or {}).get('ca_file') or '', 'paths': dict(sign_in['paths']),
                      'groups': list(sign_in['groups']), 'require': sign_in['required'],
                      'key': gateway_key(issuer, sign_in['client_id'], sign_in['paths'], host)})
    return specs, problems
