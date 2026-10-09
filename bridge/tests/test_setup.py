"""Setup-time configuration, `waveshare-bridge` CLI, mDNS advertisement, log hygiene."""
import asyncio
import contextlib
import hashlib
import io
import json
import os
import pathlib
import plistlib
import ssl
import stat
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

from waveshare_bridge import cli, config, enroll, home_sensors, mdns
from tests.test_enroll import make_cert
from tests.test_transport import ROOT

SECRET = 'fixture-client-secret-DO-NOT-LOG'


def answers(values):
    it = iter(values)
    return lambda prompt='': next(it)


class InitTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        self.dir = pathlib.Path(self.tmp.name) / 'cfg'

    def tearDown(self):
        self.tmp.cleanup()

    def run_init(self, extra=()):
        out = io.StringIO()
        prompts = answers(['10.20.30.40', '8098', 'http://127.0.0.1:9119', 'https://auth.example.com/api/oidc/token',
                           'waveshare-bridge', 'hermes.sessions.read', 'hermes.helper.command',
                           'https://hermes.example.com/api/plugins/waveshare-sessions', 'Studio Mac'])
        secret_prompts = []

        def getpass(prompt=''):
            secret_prompts.append(prompt)
            return SECRET
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            rc = cli.main(['--config-dir', str(self.dir), 'init', *extra], input_fn=prompts, getpass_fn=getpass)
        return rc, out.getvalue(), secret_prompts

    def test_init_creates_private_files_and_never_echoes_secret(self):
        rc, output, secret_prompts = self.run_init()
        self.assertEqual(rc, 0)
        self.assertEqual(len(secret_prompts), 1)  # client secret read without echo
        self.assertNotIn(SECRET, output)
        self.assertEqual(stat.S_IMODE(self.dir.stat().st_mode), 0o700)
        for name in ('bridge.json', 'authelia-client.json', 'tls.key', 'board.key'):
            self.assertEqual(stat.S_IMODE((self.dir / name).stat().st_mode), 0o600, name)
        self.assertIn(SECRET, (self.dir / 'authelia-client.json').read_text())
        self.assertNotIn(SECRET, (self.dir / 'bridge.json').read_text())
        cfg = config.load(self.dir)
        self.assertEqual(cfg['bind'], '10.20.30.40')
        self.assertIsNone(cfg['allow_ip'])
        self.assertEqual(cfg['name'], 'Studio Mac')
        fp = enroll.cert_fingerprint(self.dir / 'tls.crt')
        self.assertIn(fp.hex(), output)  # operator sees the pin it can compare
        # Re-running init never silently overwrites credentials.
        rc2, output2, _ = self.run_init()
        self.assertNotEqual(rc2, 0)
        self.assertIn('--force', output2)

    def test_launchd_plist_generated_not_committed(self):
        rc, _, _ = self.run_init(['--launchd', '--label', 'com.example.waveshare-bridge'])
        self.assertEqual(rc, 0)
        plist = self.dir / 'com.example.waveshare-bridge.plist'
        data = plistlib.loads(plist.read_bytes())
        self.assertEqual(data['Label'], 'com.example.waveshare-bridge')
        self.assertIn('run', data['ProgramArguments'])
        self.assertIn(str(self.dir), data['ProgramArguments'])
        self.assertNotIn(SECRET, plist.read_text())
        self.assertTrue(str(plist).startswith(str(self.dir)))  # written into the private config dir only
        repo = pathlib.Path(__file__).resolve().parents[2]
        tracked = subprocess.run(['git', 'ls-files', '*.plist'], cwd=repo, capture_output=True, text=True)
        self.assertEqual(tracked.stdout.strip(), '')

    def test_config_validation(self):
        self.run_init()
        cfg = json.loads((self.dir / 'bridge.json').read_text())
        for key, bad in (('bind', '8.8.8.8'), ('bind', '127.0.0.1'), ('port', 0), ('allow_ip', ['nope']),
                         ('providers', {'hermes': {'gateway': 'http://10.0.0.1:9119'}})):
            with self.subTest(key=key):
                broken = dict(cfg, **{key: bad})
                (self.dir / 'bridge.json').write_text(json.dumps(broken))
                with self.assertRaises(ValueError):
                    config.load(self.dir)
        (self.dir / 'bridge.json').write_text(json.dumps(cfg))
        (self.dir / 'bridge.json').chmod(0o644)
        with self.assertRaises(ValueError):
            config.load(self.dir)

    def test_init_parser_accepts_home_assistant_provider(self):
        parsed = cli.build_parser().parse_args(['init', '--provider', 'home_assistant'])
        self.assertEqual(parsed.provider, 'home_assistant')

    def test_home_assistant_only_init_loads_sign_in_and_starts_without_hermes(self):
        out = io.StringIO()
        prompts_seen = []
        supplied = iter([
            '10.20.30.40', '8098',
            'https://auth.example.com/api/oidc/token', 'waveshare-home', '',
            'https://sensors.example.com/api/waveshare', '', 'waveshare-phone', 'Sensor bridge',
        ])

        def prompt(text=''):
            prompts_seen.append(text)
            return next(supplied)

        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            rc = cli.cmd_init(SimpleNamespace(config_dir=self.dir, provider='home_assistant', force=False,
                              launchd=False, label='local.waveshare-bridge'),
                              input_fn=prompt, getpass_fn=lambda _prompt='': SECRET)
        self.assertEqual(rc, 0, out.getvalue())
        self.assertNotIn(SECRET, out.getvalue())
        self.assertNotIn('Hermes', ''.join(prompts_seen))
        self.assertNotIn('dashboard', ''.join(prompts_seen).lower())
        self.assertFalse((self.dir / 'authelia-client.json').exists())
        self.assertTrue((self.dir / 'home-client.json').exists())
        machine, resource = home_sensors.load_home_client(self.dir / 'home-client.json')
        self.assertEqual(machine['scope'], 'authelia.bearer.authz')
        self.assertEqual(resource['resource_url'], 'https://sensors.example.com/api/waveshare')
        raw = json.loads((self.dir / 'bridge.json').read_text())
        self.assertEqual(set(raw['providers']), {'home_assistant'})
        self.assertEqual(raw['providers']['home_assistant']['sign_in']['client_id'], 'waveshare-phone')
        cfg = config.load(self.dir)
        specs, problems = config.sign_in_specs(cfg)
        self.assertEqual(problems, {})
        self.assertEqual([(s['provider'], s['client_id'], s['require']) for s in specs],
                         [('home_assistant', 'waveshare-phone', True)])

        class FakeInner:
            port = 18098
            phone = None
            async def close(self): pass

        class FakeControl:
            def close(self): pass
            async def wait_closed(self): pass

        class FakeAdvertiser:
            async def close(self): pass

        captured = {}

        async def fake_bridge(args):
            captured['args'] = args
            return FakeInner()

        async def fake_control(*_args):
            return FakeControl()

        async def fake_advertiser(*_args):
            return FakeAdvertiser()

        async def exercise():
            with mock.patch('waveshare_bridge.live_bridge.start_bridge', side_effect=fake_bridge), \
                    mock.patch.object(enroll, 'start_control', side_effect=fake_control), \
                    mock.patch('waveshare_bridge.mdns.Advertiser.start', side_effect=fake_advertiser):
                runtime = await cli.start(cfg)
                await runtime.close()

        asyncio.run(exercise())
        args = captured['args']
        self.assertIsNone(args.authelia_client_file)
        self.assertEqual(pathlib.Path(args.home_client_file), self.dir / 'home-client.json')
        self.assertEqual(args.plugins, frozenset({'home_assistant'}))
        self.assertEqual([s['provider'] for s in args.sign_ins], ['home_assistant'])

    def test_invalid_home_assistant_force_init_preserves_existing_credentials(self):
        self.dir.mkdir(parents=True, mode=0o700)
        original = b'{"fixture":"keep"}\n'
        (self.dir / 'home-client.json').write_bytes(original)
        (self.dir / 'home-client.json').chmod(0o600)
        prompts = answers(['10.20.30.40', '8098', 'not-a-url', 'waveshare-home', '',
                           'https://sensors.example.com/api/waveshare', '', 'waveshare-phone', 'Sensor bridge'])
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            rc = cli.main(['--config-dir', str(self.dir), 'init', '--provider', 'home_assistant', '--force'],
                          input_fn=prompts, getpass_fn=lambda _prompt='': SECRET)
        self.assertEqual(rc, 2)
        self.assertEqual((self.dir / 'home-client.json').read_bytes(), original)


class PhoneCliTests(unittest.TestCase):
    def test_boards_lists_phone_column_and_revokes(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            cfg = pathlib.Path(tmp) / 'cfg'
            cfg.mkdir(mode=0o700)
            reg = enroll.Registry(cfg / 'boards.json')
            a = reg.add(bytes(range(1, 33)), 'Desk')
            b = reg.add(bytes(range(2, 34)), 'Kitchen')
            reg.set_phone(a['id'], 'samlee', 'Sam Lee', now=1790000000)

            def run(*args):
                out = io.StringIO()
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                    rc = cli.main(['--config-dir', str(cfg), 'boards', *args])
                return rc, out.getvalue()
            rc, text = run()
            self.assertEqual(rc, 0)
            self.assertIn('PHONE', text)
            self.assertIn('samlee', text)
            self.assertIn('not signed in', text)
            rc, _ = run('--revoke-phone', a['id'])
            self.assertEqual(rc, 0)
            self.assertIsNone(enroll.Registry(cfg / 'boards.json').match(bytes(range(1, 33))).get('phone_user'))
            self.assertEqual(run('--revoke-phone', 'nope')[0], 1)

    def test_bridge_json_phone_settings_validated(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = pathlib.Path(tmp)
            root.chmod(0o700)
            base = {'bind': '10.99.0.5', 'port': 8098}

            def write(sign_in):
                enroll.write_private_json(root / 'bridge.json', {**base, 'providers': {'hermes': {'sign_in': sign_in}}})
            write({})
            cfg = config.load(root)
            self.assertTrue(cfg['require_phone_auth'])
            self.assertEqual(cfg['phone_groups'], ['admins', 'hermes_users'])
            write({'required': False, 'groups': ['family']})
            cfg = config.load(root)
            self.assertEqual((cfg['require_phone_auth'], cfg['phone_groups']), (False, ['family']))
            write({'oidc_paths': {'token': '/o/token'}})
            cfg = config.load(root)
            self.assertEqual((cfg['oidc_paths']['token'], cfg['oidc_paths']['userinfo']), ('/o/token', '/api/oidc/userinfo'))
            for bad in ({'required': 1}, {'groups': []}, {'groups': 'admins'},
                        {'oidc_paths': {'token': 'https://evil.example/t'}}, {'oidc_paths': {'jwks': '/x'}}):
                write(bad)
                with self.subTest(bad=bad), self.assertRaises(ValueError):
                    config.load(root)


class MdnsTests(unittest.TestCase):
    def test_txt_record_carries_fingerprint(self):
        fp = bytes(range(32))
        info = mdns.service_info('Studio Mac', '10.20.30.40', 8098, fp)
        self.assertEqual(info.type, '_waveshare-ai._tcp.local.')
        self.assertTrue(info.name.endswith('._waveshare-ai._tcp.local.'))
        self.assertEqual(info.port, 8098)
        props = {k.decode(): v.decode() for k, v in info.properties.items()}
        self.assertEqual(props['fp'], fp.hex())
        self.assertEqual(props['name'], 'Studio Mac')
        self.assertEqual(props['v'], '1')
        self.assertEqual(info.parsed_addresses(), ['10.20.30.40'])
        self.assertLessEqual(len(props['name']), 32)
        self.assertEqual(mdns.service_info('x' * 80, '10.0.0.1', 1, fp).properties[b'name'], b'x' * 32)


class RunTests(unittest.IsolatedAsyncioTestCase):
    async def test_run_serves_enroll_and_logs_no_secrets(self):
        from tests.test_transport import lan_ip
        lan = lan_ip()
        if lan is None:
            self.skipTest('no RFC1918 interface')
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / 'cfg'
            root.mkdir(mode=0o700)
            cert, key = make_cert(root)
            (root / 'board.key').write_text('12' * 32)
            (root / 'board.key').chmod(0o600)
            enroll.write_private_json(root / 'authelia-client.json', {
                'token_endpoint': 'https://127.0.0.1:1/api/oidc/token', 'client_id': 'c',
                'client_secret': SECRET, 'scope': 'hermes.sessions.read',
                'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions'})
            import socket
            with socket.socket() as probe:
                probe.bind((lan, 0))
                free = probe.getsockname()[1]
            enroll.write_private_json(root / 'bridge.json', {
                'bind': lan, 'port': free, 'name': 'Test bridge', 'mdns': False, 'providers': {
                    'hermes': {'gateway': 'http://127.0.0.1:1', 'quota_cache': False, 'profiles_dir': str(root / 'profiles')},
                    'home_assistant': {'sign_in': {'client_file': 'authelia-client.json'}}}})
            cfg = config.load(root)
            out = io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                runtime = await cli.start(cfg)
                try:
                    import aiohttp
                    ctx = ssl.create_default_context()
                    ctx.check_hostname = False
                    ctx.verify_mode = ssl.CERT_NONE
                    token = bytes(range(1, 33))
                    body = b'WEN1' + token + b'Probe' + b'\0' * 28
                    async with aiohttp.ClientSession() as s:
                        url = f'https://{lan}:{runtime.port}'
                        async with s.post(url + '/v1/enroll', data=body, ssl=ctx) as r:
                            self.assertEqual(r.status, 403)
                        async with s.get(url + '/v1/live', ssl=ctx,
                                         headers={'Authorization': 'Bearer ' + token.hex()}) as r:
                            self.assertEqual(r.status, 401)
                        async with s.get(url + '/v1/pair/phone/status', ssl=ctx,
                                         headers={'Authorization': 'Bearer ' + token.hex(), 'X-Provider': 'hermes'}) as r:
                            self.assertEqual(r.status, 401)   # route is wired, board unknown
                        async with s.get(url + '/v1/pair/phone/nope', ssl=ctx) as r:
                            self.assertEqual(r.status, 404)
                finally:
                    await runtime.close()
            text = out.getvalue()
            self.assertIn('Bridge ready', text)
            self.assertNotIn(SECRET, text)
            self.assertNotIn(bytes(range(1, 33)).hex(), text)
            self.assertNotIn('12' * 32, text)


class MdnsLiveTests(unittest.IsolatedAsyncioTestCase):
    async def test_advertise_then_browse_finds_fingerprint(self):
        from tests.test_transport import lan_ip
        lan = lan_ip()
        if lan is None:
            self.skipTest('no RFC1918 interface')
        from zeroconf import IPVersion
        from zeroconf.asyncio import AsyncZeroconf
        fp = hashlib.sha256(b'mdns-live-test').digest()
        adv = await mdns.Advertiser('Unit test bridge', lan, 18098, fp).start()
        browser = AsyncZeroconf(interfaces=[lan], ip_version=IPVersion.V4Only)
        try:
            info = await browser.async_get_service_info(mdns.SERVICE_TYPE, adv.info.name, timeout=3000)
            self.assertIsNotNone(info)
            self.assertEqual(info.properties[b'fp'], fp.hex().encode())
            self.assertEqual(info.port, 18098)
            self.assertIn(lan, info.parsed_addresses())
        finally:
            await browser.async_close()
            await adv.close()


class PluginInstallTests(unittest.TestCase):
    def test_install_writes_settings_without_secret_and_backs_up(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = pathlib.Path(tmp)
            cfg = root / 'cfg'
            cfg.mkdir(mode=0o700)
            enroll.write_private_json(cfg / 'authelia-client.json', {
                'token_endpoint': 'https://auth.example.com/api/oidc/token', 'client_id': 'waveshare-bridge',
                'client_secret': SECRET, 'scope': 'hermes.sessions.read', 'command_scope': 'hermes.helper.command',
                'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions'})
            home = root / 'hermes'
            (home / 'plugins' / 'waveshare-sessions').mkdir(parents=True)
            (home / 'plugins' / 'waveshare-sessions' / 'old.txt').write_text('x')
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = cli.main(['--config-dir', str(cfg), 'plugin', 'install', '--hermes-home', str(home),
                               '--source', str(pathlib.Path(__file__).resolve().parents[2] / 'plugins/hermes/dashboard-plugin')])
            self.assertEqual(rc, 0)
            target = home / 'plugins' / 'waveshare-sessions'
            settings = json.loads((target / 'settings.json').read_text())
            self.assertEqual(settings['issuer'], 'https://auth.example.com')
            self.assertEqual(settings['jwks_uri'], 'https://auth.example.com/jwks.json')
            self.assertEqual(settings['client_id'], 'waveshare-bridge')
            self.assertNotIn('profile', settings)
            self.assertNotIn(SECRET, (target / 'settings.json').read_text())
            self.assertNotIn(SECRET, out.getvalue())
            self.assertTrue((target / 'plugin.yaml').exists())
            self.assertFalse((target / 'tests').exists())
            self.assertTrue(list((home / 'plugins').glob('waveshare-sessions.bak-*')))

    def test_install_takes_another_providers_issuer_and_jwks(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = pathlib.Path(tmp)
            cfg = root / 'cfg'
            cfg.mkdir(mode=0o700)
            enroll.write_private_json(cfg / 'authelia-client.json', {
                'token_endpoint': 'https://sso.example.com/realms/home/protocol/openid-connect/token',
                'client_id': 'waveshare-bridge', 'client_secret': SECRET, 'scope': 'hermes.sessions.read',
                'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions'})
            home = root / 'hermes'
            home.mkdir()
            issuer = 'https://sso.example.com/realms/home'
            jwks = issuer + '/protocol/openid-connect/certs'
            with contextlib.redirect_stdout(io.StringIO()):
                rc = cli.main(['--config-dir', str(cfg), 'plugin', 'install', '--hermes-home', str(home),
                               '--issuer', issuer + '/', '--jwks-uri', jwks])
            self.assertEqual(rc, 0)
            settings = json.loads((home / 'plugins' / 'waveshare-sessions' / 'settings.json').read_text())
            self.assertEqual((settings['issuer'], settings['jwks_uri']), (issuer, jwks))
            err = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
                rc = cli.main(['--config-dir', str(cfg), 'plugin', 'install', '--hermes-home', str(home),
                               '--jwks-uri', 'http://sso.example.com/certs'])
            self.assertEqual(rc, 2)
            self.assertIn('--jwks-uri', err.getvalue())

    def test_reinstall_preserves_existing_operator_settings(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = pathlib.Path(tmp)
            cfg = root / 'cfg'
            cfg.mkdir(mode=0o700)
            enroll.write_private_json(cfg / 'authelia-client.json', {
                'token_endpoint': 'https://auth.example.com/api/oidc/token', 'client_id': 'waveshare-bridge',
                'client_secret': SECRET, 'scope': 'hermes.sessions.read', 'command_scope': 'hermes.helper.command',
                'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions'})
            target = root / 'hermes' / 'plugins' / 'waveshare-sessions'
            target.mkdir(parents=True)
            existing = {'issuer': 'https://old.example.com', 'profile': 'helper',
                        'gadget': {'enabled': True, 'allowed_groups': ['admins']},
                        'jwks_ca_file': '/private/ca.pem', 'command_profiles': ['helper', 'atlas']}
            (target / 'settings.json').write_text(json.dumps(existing))
            with contextlib.redirect_stdout(io.StringIO()):
                rc = cli.main(['--config-dir', str(cfg), 'plugin', 'install', '--hermes-home', str(root / 'hermes')])
            self.assertEqual(rc, 0)
            settings = json.loads((target / 'settings.json').read_text())
            self.assertEqual(settings['gadget'], existing['gadget'])
            self.assertEqual(settings['jwks_ca_file'], '/private/ca.pem')
            self.assertEqual(settings['command_profiles'], ['helper', 'atlas'])
            self.assertEqual(settings['issuer'], 'https://auth.example.com')
            backups = list((root / 'hermes' / 'plugins').glob('waveshare-sessions.bak-*'))
            self.assertEqual(json.loads((backups[0] / 'settings.json').read_text()), existing)


if __name__ == '__main__':
    unittest.main()
