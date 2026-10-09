"""Providers layout of bridge.json (SPEC Contract C): per-provider sign-in gateways, ``provider list``,
``hermes sdk-setup``, and the refusal of the old flat (pre-providers) layout.

INSTALL has the shape of a real install (Hermes with every tile and SDK front, Home
Assistant sharing the Hermes sign-in). Fixture values only: example.com hosts and the 10.0.0.x
documentation range.
"""
import contextlib
import io
import json
import os
import pathlib
import stat
import sys
import tempfile
import unittest

from waveshare_bridge import cli, config, enroll, phone_pair
from tests.test_transport import ROOT

SECRET = 'fixture-client-secret-' + 'S' * 24
INSTALL = {
    'bind': '10.0.0.5', 'port': 8098, 'name': 'Waveshare AI bridge', 'mdns': True, 'allow_ip': None,
    'providers': {
        'hermes': {'gateway': 'http://127.0.0.1:9119', 'client_file': 'authelia-client.json', 'sign_in': {},
                   'tiles': ['sparkles', 'ask'], 'bots': ['helper', 'atlas', 'coding'],
                   'gadget_sdk': {'port': 8768, 'profiles': {'helper': 8775, 'atlas': 8776, 'coding': 8777}}},
        'home_assistant': {'client_file': 'home-client.json', 'sign_in': {'client_file': 'authelia-client.json'}}}}
# Missing providers and unexpected top-level settings are refused.
FLAT = {'allow_ip': None, 'bind': '10.0.0.5', 'bots': ['helper', 'atlas', 'coding'],
        'gadget_gateway': {'port': 8768, 'profiles': {'helper': 8775, 'atlas': 8776, 'coding': 8777}},
        'gadget_url': 'ws://127.0.0.1:8766/gadget', 'gateway': 'http://127.0.0.1:9119', 'hermes_transport': 'gadget',
        'mdns': True, 'name': 'Waveshare AI bridge', 'port': 8098}
HERMES_CLIENT = {'token_endpoint': 'https://auth.example.com/api/oidc/token', 'client_id': 'bridge-machine',
                 'client_secret': SECRET, 'scope': 'hermes.sessions.read', 'command_scope': 'hermes.helper.command',
                 'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions'}
# The Sensor client reaches the same provider another way (loopback + Host header): INSTALL still
# shares ONE sign-in because its Home Assistant sign-in names the Hermes client file.
HOME_CLIENT = {'token_endpoint': 'http://127.0.0.1:9091/api/oidc/token', 'token_host_header': 'auth.example.com',
               'client_id': 'sensor-machine', 'client_secret': SECRET,
               'audience': 'https://ha.example.com/api/waveshare-sensors',
               'resource_url': 'https://ha.example.com/api/waveshare-sensors'}


def make_dir(tmp, bridge_json, clients=True):
    root = pathlib.Path(tmp) / 'cfg'
    root.mkdir(mode=0o700)
    enroll.write_private_json(root / 'bridge.json', bridge_json)
    if clients:
        enroll.write_private_json(root / 'authelia-client.json', HERMES_CLIENT)
        enroll.write_private_json(root / 'home-client.json', HOME_CLIENT)
    return root


def run_cli(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = cli.main(list(argv))
    return rc, out.getvalue(), err.getvalue()


class ProvidersLayoutTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        self.root = make_dir(self.tmp.name, INSTALL)

    def tearDown(self):
        self.tmp.cleanup()

    def test_real_install_shape_is_hermes_plus_home_assistant_on_one_gateway(self):
        cfg = config.load(self.root)
        self.assertEqual(list(cfg['providers']), ['hermes', 'home_assistant'])
        hermes, home = cfg['providers']['hermes'], cfg['providers']['home_assistant']
        self.assertEqual(hermes['tiles'], ['sparkles', 'ask'])
        self.assertEqual(hermes['gadget_sdk'], {'port': 8768, 'upstream_host': '127.0.0.1',
                                                'profiles': {'helper': 8775, 'atlas': 8776, 'coding': 8777}})
        self.assertEqual((hermes['gateway'], hermes['client_file']), ('http://127.0.0.1:9119', 'authelia-client.json'))
        self.assertEqual(hermes['sign_in']['groups'], ['admins', 'hermes_users'])
        self.assertTrue(hermes['sign_in']['required'])
        self.assertEqual(home['client_file'], 'home-client.json')
        self.assertEqual(home['sign_in']['groups'], ['admins'])
        self.assertEqual(home['sign_in']['client_file'], 'authelia-client.json')
        self.assertEqual(cfg['plugins'], frozenset({'ai', 'sparkles', 'home_assistant'}))
        self.assertEqual((cfg['gadget_gateway']['port'], cfg['bind'], cfg['port']),
                         (8768, '10.0.0.5', 8098))
        self.assertEqual(cfg['client_file'], self.root / 'authelia-client.json')
        self.assertEqual(cfg['home_client_file'], self.root / 'home-client.json')
        # The Home Assistant provider shares the Hermes sign-in although home-client.json differs.
        specs, problems = config.sign_in_specs(cfg)
        self.assertEqual(problems, {})
        keys = {s['provider']: s['key'] for s in specs}
        self.assertEqual(keys['hermes'], keys['home_assistant'])
        self.assertEqual(keys['hermes'], phone_pair.gateway_key('https://auth.example.com', 'waveshare-pairing',
                                                                phone_pair.DEFAULT_OIDC_PATHS, ''))
        self.assertEqual(len(keys['hermes']), 16)

    def test_tiles_and_home_assistant_decide_the_route_groups(self):
        for tiles, home, routes in ((['ask'], False, {'ai'}), (['sparkles'], True, {'sparkles', 'home_assistant'}),
                                    ([], True, {'home_assistant'}), ([], False, set())):
            with self.subTest(tiles=tiles, home=home):
                providers = {'hermes': {**INSTALL['providers']['hermes'], 'tiles': tiles}}
                if home:
                    providers['home_assistant'] = INSTALL['providers']['home_assistant']
                cfg = config.parse({**INSTALL, 'providers': providers}, self.root)
                self.assertEqual(cfg['providers']['hermes']['tiles'], tiles)
                self.assertEqual('home_assistant' in cfg['providers'], home)
                self.assertEqual(cfg['plugins'], frozenset(routes))

    def test_flat_layout_and_unknown_keys_are_refused(self):
        for flat in (FLAT, {'bind': '10.0.0.6'}, {**FLAT, 'plugins': ['ai']}):
            with self.subTest(flat=sorted(flat)), self.assertRaisesRegex(ValueError, 'no providers object'):
                config.parse(flat, self.root)
        for mixed in ({**FLAT, 'providers': {}}, {'bind': '10.0.0.5', 'providers': {}, 'home_groups': ['a']},
                      {**INSTALL, 'unexpected_token_file': 'board.token'}):
            with self.subTest(mixed=sorted(mixed)), self.assertRaisesRegex(ValueError, 'unknown bridge.json key'):
                config.parse(mixed, self.root)
        for bad in ({'bind': '10.0.0.5', 'providers': {}, 'provider': 1},
                    {'bind': '10.0.0.5', 'providers': {'matter': {}}},
                    {'bind': '10.0.0.5', 'providers': {'hermes': {'tiles': ['ai']}}},
                    {'bind': '10.0.0.5', 'providers': {'hermes': {'gadget_gateway': {}}}},
                    {'bind': '10.0.0.5', 'providers': {'home_assistant': {'sign_in': {'required': False}}}},
                    {'bind': '10.0.0.5', 'providers': {'hermes': {'sign_in': {'issuer': 'https://a.example.com/x'}}}},
                    {'bind': '10.0.0.5', 'providers': {'hermes': {'sign_in': {'issuer': 'http://auth.example.com'}}}},
                    {'bind': '10.0.0.5', 'providers': {'hermes': {'client_file': '../x.json'}}},
                    {'bind': '10.0.0.5', 'providers': {'hermes': {'sign_in': {'groups': []}}}}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                config.parse(bad, self.root)


class SignInGatewayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        self.root = make_dir(self.tmp.name, {'bind': '10.0.0.5', 'providers': {}})

    def tearDown(self):
        self.tmp.cleanup()

    def specs(self, providers):
        return config.sign_in_specs(config.parse({'bind': '10.0.0.5', 'providers': providers}, self.root))

    def test_home_assistant_on_its_own_client_is_a_separate_gateway(self):
        specs, problems = self.specs({'hermes': {}, 'home_assistant': {}})
        self.assertEqual(problems, {})
        hermes, home = specs
        self.assertNotEqual(hermes['key'], home['key'])
        self.assertEqual((home['issuer_base'], home['host_header'], home['public_host']),
                         ('http://127.0.0.1:9091', 'auth.example.com', 'auth.example.com'))
        self.assertEqual((hermes['issuer_base'], hermes['host_header'], hermes['public_host']),
                         ('https://auth.example.com', '', 'auth.example.com'))

    def test_identical_explicit_gateways_share_and_any_difference_splits(self):
        same = {'issuer': 'https://auth.example.com', 'host_header': ''}
        specs, _ = self.specs({'hermes': {}, 'home_assistant': {'sign_in': same}})
        self.assertEqual(specs[0]['key'], specs[1]['key'])
        for change in ({'client_id': 'home-pairing'}, {'host_header': 'sso.example.com'},
                       {'oidc_paths': {'userinfo': '/other/userinfo'}}, {'issuer': 'https://sso.example.com'}):
            with self.subTest(change=change):
                specs, _ = self.specs({'hermes': {}, 'home_assistant': {'sign_in': {**same, **change}}})
                self.assertNotEqual(specs[0]['key'], specs[1]['key'])

    def test_unresolvable_gateway_is_reported_without_values(self):
        (self.root / 'home-client.json').unlink()
        specs, problems = self.specs({'hermes': {}, 'home_assistant': {}})
        self.assertEqual([s['provider'] for s in specs], ['hermes'])
        self.assertEqual(problems, {'home_assistant': 'home-client.json: FileNotFoundError'})
        specs, problems = self.specs({'home_assistant': {'sign_in': {'issuer': 'https://auth.example.com'}}})
        self.assertEqual((len(specs), problems), (1, {}))       # an explicit issuer needs no client file



class ProviderListTests(unittest.TestCase):
    def test_lists_tiles_gateway_keys_and_sharing_without_urls_or_ids(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = make_dir(tmp, INSTALL)
            rc, out, _ = run_cli('--config-dir', str(root), 'provider', 'list')
            self.assertEqual(rc, 0)
            key = config.sign_in_specs(config.load(root))[0][0]['key']
            lines = out.splitlines()
            self.assertEqual(lines[0].split(), ['PROVIDER', 'TILES', 'SIGN-IN', 'GATEWAY', 'SHARES', 'SIGN-IN', 'WITH'])
            self.assertEqual(lines[1].split(), ['hermes', 'sparkles,', 'ask', key, 'home_assistant'])
            self.assertEqual(lines[2].split(), ['home_assistant', 'sensor', key, 'hermes'])
            self.assertEqual(len(lines), 3)
            for value in ('example.com', 'waveshare-pairing', 'bridge-machine', 'sensor-machine', SECRET, '127.0.0.1',
                          'admins'):
                self.assertNotIn(value, out)
            enroll.write_private_json(root / 'bridge.json', {'bind': '10.0.0.5', 'providers': {
                'hermes': {'tiles': ['sparkles']}, 'home_assistant': {}}})
            rc, out, _ = run_cli('--config-dir', str(root), 'provider', 'list')
            lines = out.splitlines()
            self.assertEqual(lines[1].split()[:2], ['hermes', 'sparkles'])
            self.assertEqual(lines[1].split()[-1], '-')
            self.assertEqual(lines[2].split()[-1], '-')
            self.assertEqual(len(lines), 3)


FAKE_HERMES = '''#!/bin/sh
printf '%s\\n' "$*" >> "$FAKE_HERMES_LOG"
case "$*" in *"$FAKE_HERMES_FAIL"*) [ -n "$FAKE_HERMES_FAIL" ] && exit 3;; esac
exit 0
'''


class SdkSetupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=ROOT)
        base = pathlib.Path(self.tmp.name)
        self.root = make_dir(self.tmp.name, {'bind': '10.0.0.5', 'providers': {'hermes': {}}})
        self.bin = base / 'bin'
        self.bin.mkdir()
        (self.bin / 'hermes').write_text(FAKE_HERMES)
        (self.bin / 'hermes').chmod(0o755)
        self.log = base / 'hermes.log'
        self.env = mock_env({'PATH': f'{self.bin}{os.pathsep}/usr/bin:/bin', 'FAKE_HERMES_LOG': str(self.log),
                             'FAKE_HERMES_FAIL': '', 'HERMES_HOME': str(base / 'hermes-home')})
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def calls(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_print_mode_runs_nothing_and_writes_nothing(self):
        before = (self.root / 'bridge.json').read_bytes()
        rc, out, _ = run_cli('--config-dir', str(self.root), 'hermes', 'sdk-setup', '--bot', 'helper=8775',
                             '--bot', 'atlas=8776')
        self.assertEqual(rc, 0)
        self.assertEqual(self.calls(), [])
        self.assertEqual((self.root / 'bridge.json').read_bytes(), before)
        lines = [line for line in out.splitlines() if line.startswith('hermes ')]
        self.assertEqual(lines[:9], [
            'hermes -p helper plugins install Adolanium/hermes-gadget-sdk --ref '
            '75b8a689bce3925d46de5c256f53d343ae6b876f --no-enable',
            'hermes -p helper config set platforms.gadget.extra.host 127.0.0.1',
            'hermes -p helper config set platforms.gadget.extra.port 8775',
            'hermes -p helper config set platforms.gadget.extra.path /gadget',
            'hermes -p helper config set platforms.gadget.extra.speak_replies false',
            'hermes -p helper config set platforms.gadget.extra.auto_home true',
            'hermes -p helper config set platforms.gadget.extra.unauthorized_dm_behavior pair',
            'hermes -p helper config set platforms.gadget.enabled true',
            'hermes -p helper plugins enable gadget --no-allow-tool-override'])
        self.assertEqual(len(lines), 18)
        self.assertIn('hermes -p atlas config set platforms.gadget.extra.port 8776', lines)
        self.assertIn('providers.hermes.gadget_sdk = {"port": 8768, "profiles": {"helper": 8775, "atlas": 8776}}', out)
        self.assertIn('--apply', out)
        pin = (pathlib.Path(cli.__file__).resolve().parents[1] / 'pyproject.toml').read_text()
        self.assertNotIn('hermes-gadget @', pin)  # SDK installs only in bot profiles
        self.assertIn(cli.GADGET_SDK_PIN, out)

    def test_apply_runs_each_step_then_writes_bridge_json(self):
        rc, out, err = run_cli('--config-dir', str(self.root), 'hermes', 'sdk-setup', '--bot', 'helper=8775',
                               '--bot', 'coding=8777', '--port', '8770', '--apply')
        self.assertEqual((rc, err), (0, ''))
        calls = self.calls()
        self.assertEqual(len(calls), 18)
        self.assertEqual(calls[0], '-p helper plugins install Adolanium/hermes-gadget-sdk --ref '
                                   '75b8a689bce3925d46de5c256f53d343ae6b876f --no-enable')
        self.assertEqual(calls[7], '-p helper config set platforms.gadget.enabled true')
        self.assertLess(calls.index('-p coding config set platforms.gadget.extra.port 8777'),
                        calls.index('-p coding config set platforms.gadget.enabled true'))
        data = json.loads((self.root / 'bridge.json').read_text())
        self.assertEqual(data['providers']['hermes']['gadget_sdk'], {'port': 8770, 'profiles': {'helper': 8775,
                                                                                               'coding': 8777}})
        self.assertEqual(stat.S_IMODE((self.root / 'bridge.json').stat().st_mode), 0o600)
        self.assertEqual(config.load(self.root)['gadget_gateway']['profiles'], {'helper': 8775, 'coding': 8777})
        self.assertTrue(out.rstrip().endswith('OK'))

    def test_a_failed_step_stops_and_leaves_bridge_json(self):
        os.environ['FAKE_HERMES_FAIL'] = 'plugins enable'
        before = (self.root / 'bridge.json').read_bytes()
        rc, _, err = run_cli('--config-dir', str(self.root), 'hermes', 'sdk-setup', '--bot', 'helper=8775',
                             '--bot', 'atlas=8776', '--apply', '--skip-install')
        self.assertEqual(rc, 1)
        self.assertIn('failed (exit 3): hermes -p helper plugins enable gadget', err)
        self.assertEqual(len(self.calls()), 8)                  # stopped at helper's last step, atlas untouched
        self.assertFalse(any('install' in c for c in self.calls()))
        self.assertEqual((self.root / 'bridge.json').read_bytes(), before)

    def test_refuses_bad_bots_and_flat_files_before_running_anything(self):
        for argv in (['--bot', 'helper'], ['--bot', 'default=8775'], ['--bot', 'helper=8768'],
                     ['--bot', 'helper=8775', '--bot', 'atlas=8775'], ['--bot', '../x=8775']):
            with self.subTest(argv=argv):
                rc, _, _ = run_cli('--config-dir', str(self.root), 'hermes', 'sdk-setup', *argv, '--apply')
                self.assertEqual(rc, 2)
        enroll.write_private_json(self.root / 'bridge.json', FLAT)
        rc, _, err = run_cli('--config-dir', str(self.root), 'hermes', 'sdk-setup', '--bot', 'helper=8775', '--apply')
        self.assertEqual(rc, 2)
        self.assertIn('no providers object', err)
        enroll.write_private_json(self.root / 'bridge.json', {'bind': '10.0.0.5', 'providers': {'home_assistant': {}}})
        rc, _, err = run_cli('--config-dir', str(self.root), 'hermes', 'sdk-setup', '--bot', 'helper=8775', '--apply')
        self.assertEqual(rc, 2)
        self.assertIn('no hermes provider', err)
        self.assertEqual(self.calls(), [])


def mock_env(values):
    from unittest import mock
    return mock.patch.dict(os.environ, values)


class InitWritesProvidersLayoutTests(unittest.TestCase):
    def test_new_install_gets_the_providers_layout(self):
        from tests.test_setup import answers
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = pathlib.Path(tmp) / 'cfg'
            prompts = answers(['10.0.0.40', '8098', 'http://127.0.0.1:9119', 'https://auth.example.com/api/oidc/token',
                               'waveshare-bridge', 'hermes.sessions.read', 'hermes.helper.command',
                               'https://hermes.example.com/api/plugins/waveshare-sessions', 'Studio Mac'])
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                rc = cli.main(['--config-dir', str(root), 'init'], input_fn=prompts, getpass_fn=lambda p='': SECRET)
            self.assertEqual(rc, 0)
            data = json.loads((root / 'bridge.json').read_text())
            self.assertEqual(data['providers'], {'hermes': {'gateway': 'http://127.0.0.1:9119'}})
            cfg = config.load(root)
            self.assertEqual(cfg['plugins'], frozenset({'ai', 'sparkles'}))


if __name__ == '__main__':
    unittest.main()
