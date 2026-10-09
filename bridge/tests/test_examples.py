"""The placeholder configs in bridge/examples load through the bridge's real validators and name no
site: only example.com hosts, loopback and the 10.0.0.x documentation range."""
import ipaddress
import json
import os
import pathlib
import re
import shutil
import tempfile
import unittest

from waveshare_bridge import cli, config, home_sensors, phone_pair
from waveshare_bridge.authelia_client import BEARER_SCOPE, validate_client

EXAMPLES = pathlib.Path(__file__).resolve().parents[1] / 'examples'
ROOT = pathlib.Path(__file__).resolve().parents[2]


def private_copy(name, target_dir, target_name):
    path = pathlib.Path(target_dir) / target_name
    shutil.copy(EXAMPLES / name, path)
    os.chmod(path, 0o600)
    return path


class ExampleConfigTests(unittest.TestCase):
    def test_bridge_example_loads_with_every_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / 'cfg'
            root.mkdir(mode=0o700)
            private_copy('bridge.example.json', root, 'bridge.json')
            private_copy('authelia-client.example.json', root, 'authelia-client.json')
            private_copy('home-client.example.json', root, 'home-client.json')
            cfg = config.load(root)
            specs, problems = config.sign_in_specs(cfg)
        self.assertEqual(cfg['bind'], '10.0.0.10')
        self.assertEqual(list(cfg['providers']), ['hermes', 'home_assistant'])
        hermes = cfg['providers']['hermes']
        self.assertEqual(hermes['tiles'], ['sparkles', 'ask'])
        self.assertEqual(cfg['plugins'], frozenset(config.PLUGINS))
        self.assertEqual(cfg['gateway'], 'http://127.0.0.1:9119')
        self.assertTrue(cfg['require_phone_auth'])
        self.assertEqual(cfg['oidc_paths'], phone_pair.DEFAULT_OIDC_PATHS)
        self.assertEqual(cfg['gadget_gateway']['profiles'], {'helper': 8775, 'atlas': 8776, 'coding': 8777})
        self.assertEqual(cfg['home_groups'], ['admins'])
        # Every key of the layout appears in the example, so it documents them all.
        raw = json.loads((EXAMPLES / 'bridge.example.json').read_text())
        self.assertEqual(set(raw['providers']['hermes']) | {'profiles_dir', 'quota_cache', 'bot_providers'},
                         set(config.HERMES_KEYS))
        self.assertEqual(set(raw['providers']['hermes']['sign_in']) | {'client_file'}, set(config.SIGN_IN_KEYS))
        # Both example clients name the same provider: one shared phone sign-in.
        self.assertEqual(problems, {})
        self.assertEqual(len({spec['key'] for spec in specs}), 1)

    def test_hermes_client_example_validates(self):
        value = json.loads((EXAMPLES / 'authelia-client.example.json').read_text())
        client = validate_client(value)
        self.assertEqual((client['scope'], client['command_scope']), ('hermes.sessions.read', 'hermes.helper.command'))

    def test_home_client_example_validates(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = private_copy('home-client.example.json', tmp, 'home-client.json')
            client, resource = home_sensors.load_home_client(path)
        self.assertEqual(client['scope'], BEARER_SCOPE)
        self.assertEqual(resource['resource_url'], client['audience'])

    def test_sensor_endpoint_example_is_the_documented_contract(self):
        body = json.loads((EXAMPLES / 'sensor-endpoint.example.json').read_text())
        readings = home_sensors._readings(body)
        assert readings is not None
        self.assertEqual(set(readings), set(home_sensors.KEYS))
        state, rows = home_sensors.decode_frame(home_sensors.encode_frame(home_sensors.ST_OK, readings))
        self.assertEqual(state, home_sensors.ST_OK)
        self.assertEqual(rows['temperature'][0], 22.4)
        self.assertIsNone(rows['pm10'][0])    # 'unavailable' shows as "--"

    def test_examples_name_no_site(self):
        for path in sorted(EXAMPLES.glob('*.json')):
            text = path.read_text()
            for host in re.findall(r'https?://([^/:"]+)', text):
                self.assertTrue(host == '127.0.0.1' or host == 'example.com' or host.endswith('.example.com'),
                                (path.name, host))
            for ip in re.findall(r'(?<![0-9.])(?:[0-9]{1,3}\.){3}[0-9]{1,3}(?![0-9])', text):
                address = ipaddress.IPv4Address(ip)
                self.assertTrue(address.is_loopback or address in ipaddress.ip_network('10.0.0.0/24'), (path.name, ip))

    def test_setup_docs_match_strict_provider_schema_and_durable_phone_state(self):
        bridge = (ROOT / 'bridge' / 'README.md').read_text()
        hermes = (ROOT / 'plugins' / 'hermes' / 'SETUP.md').read_text()
        home = (ROOT / 'plugins' / 'home_assistant' / 'SETUP.md').read_text()
        self.assertIn('`required` is valid only for Hermes', bridge)
        self.assertIn('Home Assistant phone authorization is always required', bridge)
        self.assertIn('waveshare-bridge init --provider home_assistant', home)
        self.assertIn('`ca_file` affects only the machine token endpoint TLS', home)
        self.assertIn('resource URL uses system CA trust', home)
        self.assertIn('durable local state', home)
        self.assertIn('approve again after group changes', home)
        self.assertNotIn('losing the required group closes', hermes)
        self.assertNotIn('JWKS URI and absolute device-authorization, token and userinfo paths in `bridge.json`', hermes)
        self.assertIn('plugin install --jwks-uri', hermes)

        # The schema documented above: required is Hermes-only and HA phone authorization is mandatory.
        config.parse({'bind': '10.0.0.10', 'providers': {
            'hermes': {'sign_in': {'required': False}},
            'home_assistant': {'sign_in': {'groups': ['admins']}},
        }}, pathlib.Path('/synthetic/config'))
        with self.assertRaises(ValueError):
            config.parse({'bind': '10.0.0.10', 'providers': {
                'home_assistant': {'sign_in': {'required': False}},
            }}, pathlib.Path('/synthetic/config'))

    def test_documented_jwks_override_is_accepted_by_the_real_parser(self):
        with tempfile.TemporaryDirectory() as tmp:
            parsed = cli.build_parser().parse_args([
                '--config-dir', str(pathlib.Path(tmp) / 'cfg'), 'plugin', 'install',
                '--hermes-home', str(pathlib.Path(tmp) / 'hermes'),
                '--jwks-uri', 'https://auth.example.com/keys',
            ])
        self.assertEqual(parsed.jwks_uri, 'https://auth.example.com/keys')
        self.assertNotIn('jwks_uri', config.TOP_KEYS + config.HERMES_KEYS + config.SIGN_IN_KEYS)
