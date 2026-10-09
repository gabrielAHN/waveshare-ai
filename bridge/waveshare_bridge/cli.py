"""``waveshare-bridge``: set up and run the Waveshare AI LAN bridge on a provider host.

  waveshare-bridge init [--launchd]     create ~/.config/waveshare-ai (prompts; secrets without echo)
  waveshare-bridge run                  serve the board API (TLS) + advertise _waveshare-ai._tcp
  waveshare-bridge enroll [--seconds N] open an enrollment window and confirm a board's code
  waveshare-bridge status               configuration, fingerprint, enrolled boards
  waveshare-bridge boards [--remove ID | --revoke-phone ID]
                                        list boards (with phone sign-in), remove or sign out
  waveshare-bridge plugin install       install the Hermes plugin with settings for this setup
  waveshare-bridge provider list        configured providers, their tiles and shared sign-ins
  waveshare-bridge hermes sdk-setup --bot NAME=PORT [--bot ...] [--apply]
                                        the Hermes steps for the stock Gadget SDK voice path

Secrets never appear in arguments, stdout, logs or the repository.
"""
import argparse
import asyncio
import getpass
import hashlib
import ipaddress
import json
import os
import pathlib
import plistlib
import re
import secrets
import shutil
import subprocess
import sys

from . import config as config_mod
from . import enroll as enroll_mod


def _private_dir(path):
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path, 0o700)


def _write_private_text(path, text):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    with os.fdopen(fd, 'w') as handle:
        handle.write(text)


def _ask(input_fn, prompt, default=''):
    value = input_fn(f'{prompt}{f" [{default}]" if default else ""}: ').strip()
    return value or default


def guess_lan_ip():
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(('192.0.2.1', 9))  # route lookup only; nothing is sent
            ip = s.getsockname()[0]
            return ip if ipaddress.IPv4Address(ip).is_private else ''
        except OSError:
            return ''


def make_certificate(directory, name):
    cert, key = directory / 'tls.crt', directory / 'tls.key'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-days', '3650',
                    '-keyout', str(key), '-out', str(cert), '-subj', '/CN=waveshare-bridge',
                    '-addext', 'basicConstraints=critical,CA:FALSE'],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.chmod(key, 0o600)
    os.chmod(cert, 0o644)
    return enroll_mod.cert_fingerprint(cert)


def launchd_plist(directory, label, python=sys.executable):
    log = directory / 'bridge.log'
    return {
        'Label': label,
        'ProgramArguments': [python, '-m', 'waveshare_bridge.cli', '--config-dir', str(directory), 'run'],
        'RunAtLoad': True, 'KeepAlive': True,
        'EnvironmentVariables': {'PYTHONUNBUFFERED': '1', 'PYTHONDONTWRITEBYTECODE': '1'},
        'StandardOutPath': str(log), 'StandardErrorPath': str(log),
    }


def cmd_init(args, input_fn, getpass_fn):
    root = args.config_dir
    if (root / 'bridge.json').exists() and not args.force:
        print(f'{root} is already initialised; re-run with --force to replace bridge.json/credentials '
              '(the TLS certificate and board registry are kept).', file=sys.stderr)
        return 2
    _private_dir(root)
    print(f'Waveshare AI bridge setup -> {root}')
    try:
        bind = _ask(input_fn, 'LAN IPv4 address to serve on', guess_lan_ip())
        port = int(_ask(input_fn, 'Port', '8098'))
        if args.provider == 'home_assistant':
            from .authelia_client import BEARER_SCOPE, validate_client
            from .home_sensors import _resource
            from .phone_pair import DEFAULT_CLIENT_ID
            token_endpoint = _ask(input_fn, 'Machine token endpoint (e.g. https://auth.example.com/api/oidc/token)')
            client_id = _ask(input_fn, 'Machine OAuth2 client id', 'waveshare-home')
            scope = _ask(input_fn, 'Proxy bearer scope', BEARER_SCOPE)
            resource_url = _ask(input_fn, 'Sensor resource URL')
            audience = _ask(input_fn, 'Token audience', resource_url)
            phone_client = _ask(input_fn, 'Phone OAuth2 public client id', DEFAULT_CLIENT_ID)
            name = _ask(input_fn, 'Bridge name shown on the device', 'Waveshare AI bridge')[:32]
            secret = getpass_fn('Machine OAuth2 client secret (not echoed): ')
            cfg = {'bind': config_mod.rfc1918(bind), 'port': port, 'name': name, 'mdns': True, 'allow_ip': None,
                   'providers': {'home_assistant': {'sign_in': {'client_id': phone_client}}}}
            client = {'token_endpoint': token_endpoint, 'client_id': client_id, 'client_secret': secret,
                      'scope': scope, 'audience': audience, 'resource_url': resource_url}
            resource = _resource(client)
            validate_client({k: v for k, v in client.items() if k not in resource}, scope_rule='bearer')
            client_path = root / 'home-client.json'
        else:
            from .authelia_client import validate_client
            gateway = _ask(input_fn, 'Hermes dashboard URL on this host', 'http://127.0.0.1:9119')
            token_endpoint = _ask(input_fn, 'Sign-in provider token endpoint (e.g. https://auth.example.com/api/oidc/token)')
            client_id = _ask(input_fn, 'OAuth2 client id', 'waveshare-bridge')
            scope = _ask(input_fn, 'Read scope', 'hermes.sessions.read')
            command_scope = _ask(input_fn, 'Bot capability scope', 'hermes.helper.command')
            audience = _ask(input_fn, 'Token audience (Hermes plugin URL)')
            name = _ask(input_fn, 'Bridge name shown on the device', 'Waveshare AI bridge')[:32]
            secret = getpass_fn('OAuth2 client secret (not echoed): ')
            cfg = {'bind': config_mod.rfc1918(bind), 'port': port, 'name': name, 'mdns': True, 'allow_ip': None,
                   'providers': {'hermes': {'gateway': config_mod.loopback_gateway(gateway)}}}
            client = {'token_endpoint': token_endpoint, 'client_id': client_id, 'client_secret': secret,
                      'scope': scope, 'command_scope': command_scope, 'audience': audience}
            validate_client(client)
            client_path = root / 'authelia-client.json'
        config_mod.parse(cfg, root)
    except ValueError as error:
        if 'client' in locals():
            secret = client['client_secret'] = None
        print(f'OAuth2 client settings rejected: {error}', file=sys.stderr)
        return 2
    enroll_mod.write_private_json(client_path, client)
    secret = client['client_secret'] = None
    enroll_mod.write_private_json(root / 'bridge.json', cfg)
    if not (root / 'board.key').exists():
        _write_private_text(root / 'board.key', secrets.token_hex(32))
    if (root / 'tls.crt').exists() and (root / 'tls.key').exists():
        fp = enroll_mod.cert_fingerprint(root / 'tls.crt')
    else:
        fp = make_certificate(root, name)
    print(f'TLS certificate SHA-256 (boards pin this): {fp.hex()}')
    if args.launchd:
        plist = root / f'{args.label}.plist'
        plist.write_bytes(plistlib.dumps(launchd_plist(root, args.label)))
        print(f'launchd plist written to {plist}\n  install: cp "{plist}" ~/Library/LaunchAgents/ && '
              f'launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/{plist.name}')
    print('Next: waveshare-bridge run   (then on the device: Settings > Connect to bridge, '
          'and here: waveshare-bridge enroll)')
    return 0


class Runtime:
    def __init__(self, inner, advertiser, control, port, front=None):
        self.inner, self.advertiser, self.control, self.port = inner, advertiser, control, port
        self.front = front

    async def close(self):
        if self.front is not None:
            await self.front.close()
        if self.control is not None:
            self.control.close()
            await self.control.wait_closed()
        if self.advertiser is not None:
            await self.advertiser.close()
        await self.inner.close()


async def start(cfg):
    from types import SimpleNamespace
    from . import live_bridge
    registry = enroll_mod.Registry(cfg['boards'])
    registry.boards()  # validate permissions before serving
    fingerprint = enroll_mod.cert_fingerprint(cfg['cert'])
    window = enroll_mod.EnrollWindow(registry, fingerprint)
    providers = cfg['providers']
    print('Providers: %s.' % ('; '.join('%s (%s)' % (name, ', '.join(config_mod.provider_tiles(name, p)) or 'no tiles')
                                        for name, p in providers.items()) or 'none'), flush=True)
    specs, problems = config_mod.sign_in_specs(cfg)
    for name, reason in problems.items():
        print(f'Phone sign-in {name}: off ({reason}).', flush=True)
    args = SimpleNamespace(
        authelia_client_file=str(cfg['client_file']) if 'hermes' in providers else None,
        board_key_file=str(cfg['board_key']),
        cert=str(cfg['cert']), key=str(cfg['key']), bind=cfg['bind'], port=cfg['port'],
        allow_ip=cfg['allow_ip'], gateway=cfg['gateway'],
        level_thresholds=cfg['level_thresholds'] or live_bridge.DEFAULT_THRESHOLDS,
        usage_interval=cfg['usage_interval'], ema_seconds=cfg['ema_seconds'], quota_cache=cfg.get('quota_cache'),
        bots=cfg.get('bots'), bot_providers=cfg.get('bot_providers'), bot_names=cfg.get('bot_names'),
        profiles_dir=cfg.get('profiles_dir'), authorizer=enroll_mod.Authorizer(registry), enroll=window,
        registry=registry, plugins=cfg.get('plugins'), sign_ins=specs,
        home_client_file=str(cfg['home_client_file']) if cfg.get('home_client_file') else None)
    inner = await live_bridge.start_bridge(args)
    advertiser = control = front = None
    try:
        if cfg.get('gadget_gateway'):
            from . import gadget_front
            gw = cfg['gadget_gateway']
            front = await gadget_front.start(
                gw, bind=cfg['bind'], cert=str(cfg['cert']), key=str(cfg['key']), authorizer=args.authorizer,
                phone=getattr(inner, 'phone', None),
                grants=gadget_front.Grants(lambda profile: gadget_front.approved_path(cfg['profiles_dir'], profile),
                                           cfg['gadget_grants']))
            print(f"Hermes gadget front: wss :{front.port}/gadget/<bot> -> gateway profiles "
                  f"{', '.join(sorted(gw['profiles']))}.", flush=True)
        control = await enroll_mod.start_control(cfg['control'], window)
        if cfg['mdns']:
            from .mdns import Advertiser
            advertiser = await Advertiser(cfg['name'], cfg['bind'], inner.port, fingerprint).start()
    except BaseException:
        if control is not None:
            control.close()
        if front is not None:
            await front.close()
        await inner.close()
        raise
    print(f"Bridge ready (TLS {cfg['bind']}:{inner.port}, fingerprint {fingerprint[:8].hex()}..., "
          f"{len(registry.boards())} enrolled board(s), mDNS {'on' if advertiser else 'off'}"
          f").", flush=True)
    return Runtime(inner, advertiser, control, inner.port, front)


def cmd_run(args):
    import logging
    import signal
    cfg = config_mod.load(args.config_dir)
    logging.disable(logging.CRITICAL)  # never log upstream URLs, requests or credentials

    async def main():
        runtime = await start(cfg)
        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, stop.set)
        try:
            await stop.wait()
        finally:
            await runtime.close()
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        return 130
    except Exception as error:  # message only: never a traceback with request state
        print(f'Bridge startup/runtime failure ({type(error).__name__}); check {args.config_dir}.', file=sys.stderr)
        return 1
    return 0


def cmd_enroll(args, input_fn):
    sock = args.config_dir / 'control.sock'
    if not sock.exists():
        print('The bridge is not running (start it with `waveshare-bridge run` or launchd).', file=sys.stderr)
        return 2
    outcome = asyncio.run(enroll_mod.run_enroll_client(sock, args.seconds, ask=input_fn, emit=print))
    return 0 if outcome == 'accepted' else 1


def cmd_status(args):
    root = args.config_dir
    try:
        cfg = config_mod.load(root)
    except (FileNotFoundError, ValueError) as error:
        print(f'Not configured ({error}). Run: waveshare-bridge init', file=sys.stderr)
        return 2
    fp = enroll_mod.cert_fingerprint(cfg['cert'])
    boards = enroll_mod.Registry(cfg['boards']).boards()
    print(json.dumps({'config_dir': str(root),
                      'providers': {name: config_mod.provider_tiles(name, p) for name, p in cfg['providers'].items()},
                      'plugins': sorted(cfg['plugins']),
                      'bind': cfg['bind'], 'port': cfg['port'], 'gateway': cfg['gateway'],
                      'name': cfg['name'], 'mdns': cfg['mdns'], 'allow_ip': cfg['allow_ip'],
                      'fingerprint_sha256': fp.hex(), 'running': (root / 'control.sock').exists(),
                      'require_phone_auth': cfg['require_phone_auth'],
                      'boards': [{'id': b['id'], 'name': b['name'], 'phone': phone_label(b)} for b in boards]},
                     indent=2))
    return 0


def phone_label(board):
    if board.get('phone_user'):
        import datetime
        at = datetime.datetime.fromtimestamp(board.get('phone_at') or 0).strftime('%Y-%m-%d %H:%M')
        label = f"{board['phone_user']} ({at})"
    else:
        label = 'not signed in'
    others = len(board.get('sign_ins') or {})
    return label + (f' +{others} other sign-in(s)' if others else '')


def cmd_boards(args):
    reg = enroll_mod.Registry(args.config_dir / 'boards.json')
    for flag, action, done in ((args.remove, reg.remove, 'removed'),
                               (args.revoke_phone, reg.sign_out, 'phone sign-in revoked')):
        if flag:
            ok = action(flag)
            print(done if ok else 'no such board')
            return 0 if ok else 1
    print(f"{'ID':8}  {'NAME':20}  PHONE")
    for b in reg.boards():
        print(f"{b['id']:8}  {b['name'][:20]:20}  {phone_label(b)}")
    return 0


def plugin_settings(client):
    """Non-secret Hermes plugin settings derived from the bridge's OAuth2 client settings."""
    from urllib.parse import urlsplit
    parts = urlsplit(client['token_endpoint'])
    issuer = f'{parts.scheme}://{parts.netloc}'
    out = {'issuer': issuer, 'audience': client['audience'], 'client_id': client['client_id'],
           'scope': client['scope'], 'command_scope': client.get('command_scope', ''),
           'jwks_uri': issuer + '/jwks.json',
           'command_profiles': ['helper', 'atlas', 'coding']}
    if client.get('token_host_header'):
        out['jwks_host_header'] = client['token_host_header']
    return out


def _plugin_url(value):
    from urllib.parse import urlsplit
    parts = urlsplit(value)
    if (parts.scheme not in ('https', 'http') or not parts.hostname or parts.username or parts.password
            or parts.query or parts.fragment or (parts.scheme == 'http' and parts.hostname != '127.0.0.1')):
        raise ValueError('must be https://HOST[/PATH] (or http://127.0.0.1) without credentials or query')
    return value


def _plugin_name(source):
    """The `name:` line of the plugin's plugin.yaml (hand-parsed: the venv has no YAML library)."""
    for line in (source / 'plugin.yaml').read_text().splitlines():
        match = re.fullmatch(r'name:\s*["\']?([A-Za-z0-9._-]+)["\']?\s*', line)
        if match:
            return match.group(1)
    return source.name


def default_plugin_source():
    """Dashboard plugin source in a checkout or bundled beside this module in an installed wheel."""
    checkout = pathlib.Path(__file__).resolve().parents[2] / 'plugins' / 'hermes' / 'dashboard-plugin'
    bundled = pathlib.Path(__file__).resolve().with_name('dashboard_plugin')
    return checkout if (checkout / 'plugin.yaml').is_file() else bundled


def cmd_plugin_install(args):
    from .authelia_client import load_client_file
    source = pathlib.Path(args.source).resolve()
    if not (source / 'plugin.yaml').exists():
        print(f'{source} is not the plugin directory (plugins/hermes/dashboard-plugin).', file=sys.stderr)
        return 2
    # Installed under the plugin's own name (plugin.yaml `name:`, waveshare-sessions), not the name of
    # the folder it is copied from.
    target = pathlib.Path(args.hermes_home).expanduser() / 'plugins' / _plugin_name(source)
    settings = plugin_settings(load_client_file(args.config_dir / 'authelia-client.json'))
    existing = {}
    if (target / 'settings.json').exists():
        try:
            existing = json.loads((target / 'settings.json').read_text())
        except (OSError, ValueError):
            existing = None
        if type(existing) is not dict:
            print(f'{target / "settings.json"} is not a JSON object; refusing to replace it.', file=sys.stderr)
            return 2
    # Re-installs refresh only the Authelia client identity; every other operator key
    # (bot lists, JWKS/CA overrides, ...) is preserved exactly.
    identity = ('issuer', 'audience', 'client_id', 'scope', 'command_scope')
    settings = {**settings, **existing, **{k: settings[k] for k in identity}}
    # Providers other than Authelia: their issuer (the JWT ``iss``) and JWKS URL, as published in
    # <issuer>/.well-known/openid-configuration. Default: scheme://host of the token endpoint + /jwks.json.
    for key in ('issuer', 'jwks_uri'):
        value = getattr(args, key, None)
        if value:
            try:
                _plugin_url(value)
            except ValueError as error:
                print(f'--{key.replace("_", "-")}: {error}', file=sys.stderr)
                return 2
            settings[key] = value.rstrip('/') if key == 'issuer' else value
    if target.exists():
        backup = target.with_name(f'{target.name}.bak-{secrets.token_hex(3)}')
        shutil.copytree(target, backup, ignore=shutil.ignore_patterns('__pycache__'))
        print(f'Backed up existing plugin to {backup}')
    shutil.copytree(source, target, dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__', 'tests'))
    (target / 'settings.json').write_text(json.dumps(settings, indent=2) + '\n')
    print(f'Installed {target.name} -> {target} (settings.json written; no secrets).')
    print('Enable it with `hermes plugins enable waveshare-sessions` and restart the dashboard yourself.')
    return 0


# --------------------------------------------------------------------------- bridge.json editing

def _write_json_private(path, value):
    """Atomic 0600 JSON write that keeps the key order (bridge.json stays readable top-down)."""
    path = pathlib.Path(path)
    tmp = path.with_name(f'.{path.name}.{secrets.token_hex(4)}.tmp')
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    try:
        with os.fdopen(fd, 'w') as handle:
            json.dump(value, handle, indent=2)
            handle.write('\n')
        os.replace(tmp, path)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise


def _read_bridge_json(root):
    """(raw bridge.json object, path); ValueError/OSError with a value-free message."""
    config_mod.check_private(root, 'dir')
    path = root / 'bridge.json'
    config_mod.check_private(path)
    try:
        data = json.loads(path.read_text())
    except json.JSONDecodeError:
        raise ValueError('bridge.json is not JSON') from None
    if not isinstance(data, dict):
        raise ValueError('bridge.json must be an object')
    return data, path


def cmd_provider_list(args):
    """Each configured provider, its tiles, its sign-in gateway key and who shares that sign-in.
    No URLs, client ids or secrets."""
    try:
        cfg = config_mod.load(args.config_dir)
    except (OSError, ValueError) as error:
        print(f'Not configured ({error}). Run: waveshare-bridge init', file=sys.stderr)
        return 2
    specs, problems = config_mod.sign_in_specs(cfg)
    keys = {spec['provider']: spec['key'] for spec in specs}
    rows = [('PROVIDER', 'TILES', 'SIGN-IN GATEWAY', 'SHARES SIGN-IN WITH')]
    for name, provider in cfg['providers'].items():
        key = keys.get(name)
        shared = sorted(other for other, k in keys.items() if key is not None and k == key and other != name)
        rows.append((name, ', '.join(config_mod.provider_tiles(name, provider)) or 'none',
                     key or f'unresolved ({problems.get(name, "?")})', ', '.join(shared) or '-'))
    widths = [max(len(row[i]) for row in rows) for i in range(3)]
    for row in rows:
        print('  '.join(cell.ljust(widths[i]) if i < 3 else cell for i, cell in enumerate(row)).rstrip())
    if not cfg['providers']:
        print('(no providers configured)')
    return 0


GADGET_SDK_SOURCE = 'Adolanium/hermes-gadget-sdk'
GADGET_SDK_PIN = '75b8a689bce3925d46de5c256f53d343ae6b876f'   # stock SDK plugin release pin
GADGET_SDK_PLUGIN = 'gadget'
DEFAULT_GADGET_SDK_PORT = 8768   # firmware CONFIG_WAVESHARE_AI_GATEWAY_PORT


def sdk_steps(port, install=True):
    """The Hermes CLI steps (after ``hermes -p <bot>``) for one bot profile's stock Gadget SDK platform.
    Host and port are set BEFORE ``enabled``, so the adapter never starts on its all-interfaces default."""
    steps = [['plugins', 'install', GADGET_SDK_SOURCE, '--ref', GADGET_SDK_PIN, '--no-enable']] if install else []
    for key, value in (('host', '127.0.0.1'), ('port', str(port)), ('path', '/gadget'), ('speak_replies', 'false'),
                       ('auto_home', 'true'), ('unauthorized_dm_behavior', 'pair')):
        steps.append(['config', 'set', f'platforms.gadget.extra.{key}', value])
    steps.append(['config', 'set', 'platforms.gadget.enabled', 'true'])
    steps.append(['plugins', 'enable', GADGET_SDK_PLUGIN, '--no-allow-tool-override'])
    return steps


def _parse_bots(values, front_port):
    profiles = {}
    for item in values:
        name, sep, port = item.partition('=')
        if not sep or not port.isdigit() or name in profiles:
            raise ValueError(f'--bot must be NAME=PORT (once per bot): {item!r}')
        profiles[name] = int(port)
    from .bots import valid_bot
    if not all(valid_bot(name) for name in profiles):
        raise ValueError('bot names must be Hermes profile ids (not default)')
    return config_mod.gadget_gateway({'gadget_sdk': {'port': front_port, 'profiles': profiles}}, 'gadget_sdk')


def cmd_hermes_sdk_setup(args):
    """Print (default) or run (--apply) the per-profile Hermes steps for the stock Gadget SDK and write
    bridge.json ``providers.hermes.gadget_sdk``."""
    import shlex
    try:
        sdk = _parse_bots(args.bot, args.port)
    except ValueError as error:
        print(f'hermes sdk-setup: {error}', file=sys.stderr)
        return 2
    value = {'port': sdk['port'], 'profiles': sdk['profiles']}
    data = path = None
    if args.apply:
        try:
            data, path = _read_bridge_json(args.config_dir)
            config_mod.parse(data, args.config_dir)
        except (OSError, ValueError) as error:
            print(f'bridge.json does not load ({error}); nothing was run.', file=sys.stderr)
            return 2
        if 'hermes' not in data['providers']:
            print('bridge.json has no hermes provider; nothing was run.', file=sys.stderr)
            return 2
    hermes = shutil.which('hermes') if args.apply else 'hermes'
    if hermes is None:
        print('hermes is not on PATH; nothing was run.', file=sys.stderr)
        return 2
    print(f'Hermes Gadget SDK ({GADGET_SDK_SOURCE} at {GADGET_SDK_PIN[:12]}) for {len(sdk["profiles"])} bot profile(s):')
    for bot, port in sdk['profiles'].items():
        print(f'\n# {bot}: gadget platform on 127.0.0.1:{port}/gadget')
        for step in sdk_steps(port, install=not args.skip_install):
            argv = ['hermes', '-p', bot, *step]
            print(shlex.join(argv))
            if args.apply:
                sys.stdout.flush()
                result = subprocess.run([hermes, '-p', bot, *step])
                if result.returncode != 0:
                    print(f'failed (exit {result.returncode}): {shlex.join(argv)}; bridge.json not changed.',
                          file=sys.stderr)
                    return 1
    print('\n# bridge.json')
    print('providers.hermes.gadget_sdk = ' + json.dumps(value))
    if args.apply:
        data['providers']['hermes']['gadget_sdk'] = value
        config_mod.parse(data, args.config_dir)
        _write_json_private(path, data)
        print('bridge.json: providers.hermes.gadget_sdk written.')
    print('\nNext: restart each profile\'s gateway (`hermes -p <bot> gateway restart`) and the bridge, then check '
          'that nothing listens on *:8765 (an adapter started with its defaults binds every interface).')
    if not args.apply:
        print('Printed only: run again with --apply to run these steps and write bridge.json.')
    else:
        print('OK')
    return 0


def build_parser():
    parser = argparse.ArgumentParser(prog='waveshare-bridge', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--config-dir', type=pathlib.Path, default=None,
                        help='Configuration directory (default ~/.config/waveshare-ai or $WAVESHARE_AI_CONFIG).')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('init', help='create the configuration (interactive)')
    p.add_argument('--provider', choices=('hermes', 'home_assistant'), default='hermes',
                   help='provider to configure (default: hermes)')
    p.add_argument('--force', action='store_true')
    p.add_argument('--launchd', action='store_true', help='also write a launchd plist into the config dir')
    p.add_argument('--label', default='local.waveshare-bridge')
    sub.add_parser('run', help='run the bridge')
    p = sub.add_parser('enroll', help='open an enrollment window and confirm a board')
    p.add_argument('--seconds', type=int, default=180)
    sub.add_parser('status', help='show configuration and enrolled boards')
    p = sub.add_parser('boards', help='list / remove boards, manage phone sign-in')
    p.add_argument('--remove', metavar='ID')
    p.add_argument('--revoke-phone', metavar='ID', help='sign the board out (it must sign in with the phone again)')
    p = sub.add_parser('plugin', help='Hermes plugin helpers')
    psub = p.add_subparsers(dest='plugin_command', required=True)
    pi = psub.add_parser('install', help='copy the plugin into HERMES_HOME/plugins and write settings.json')
    pi.add_argument('--source', default=str(default_plugin_source()))
    pi.add_argument('--hermes-home', default=os.environ.get('HERMES_HOME') or '~/.hermes')
    pi.add_argument('--issuer', help="your provider's issuer (JWT iss); default scheme://host of the token endpoint")
    pi.add_argument('--jwks-uri', dest='jwks_uri', help="your provider's JWKS URL; default <issuer>/jwks.json (Authelia)")
    p = sub.add_parser('provider', help='providers configured in bridge.json')
    prsub = p.add_subparsers(dest='provider_command', required=True)
    prsub.add_parser('list', help='each provider, its tiles, its sign-in gateway and who shares it')
    p = sub.add_parser('hermes', help='Hermes provider helpers')
    hsub = p.add_subparsers(dest='hermes_command', required=True)
    hs = hsub.add_parser('sdk-setup', help='the per-profile Hermes steps for the stock Gadget SDK voice path')
    hs.add_argument('--bot', action='append', required=True, metavar='NAME=PORT',
                    help="a bot profile and its gadget listener's loopback port (repeat per bot)")
    hs.add_argument('--port', type=int, default=DEFAULT_GADGET_SDK_PORT,
                    help='the LAN port boards dial for the gadget front (firmware CONFIG_WAVESHARE_AI_GATEWAY_PORT, 8768)')
    hs.add_argument('--apply', action='store_true',
                    help='run the steps with `hermes -p <bot> ...` and write providers.hermes.gadget_sdk')
    hs.add_argument('--skip-install', action='store_true', help='the SDK plugin is already installed in every profile')
    return parser


def main(argv=None, input_fn=input, getpass_fn=getpass.getpass):
    args = build_parser().parse_args(argv)
    args.config_dir = (args.config_dir or config_mod.default_dir()).expanduser()
    if args.command == 'init':
        return cmd_init(args, input_fn, getpass_fn)
    if args.command == 'run':
        return cmd_run(args)
    if args.command == 'enroll':
        return cmd_enroll(args, input_fn)
    if args.command == 'status':
        return cmd_status(args)
    if args.command == 'boards':
        return cmd_boards(args)
    if args.command == 'plugin':
        return cmd_plugin_install(args)
    if args.command == 'provider':
        return cmd_provider_list(args)
    if args.command == 'hermes':
        return cmd_hermes_sdk_setup(args)
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
