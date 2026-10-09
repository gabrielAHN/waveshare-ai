"""Installed distribution and public import/CLI naming contract."""
import importlib.util
import pathlib
import tomllib
import unittest

from waveshare_bridge import cli


ROOT = pathlib.Path(__file__).resolve().parents[1]


class DistributionContractTests(unittest.TestCase):
    def test_distribution_package_and_cli_use_only_waveshare_names(self):
        project = tomllib.loads((ROOT / 'pyproject.toml').read_text())
        self.assertEqual(project['project']['name'], 'waveshare-bridge')
        self.assertEqual(project['project']['scripts'], {'waveshare-bridge': 'waveshare_bridge.cli:main'})
        self.assertEqual(project['tool']['setuptools']['packages'], ['waveshare_bridge'])
        self.assertIsNotNone(importlib.util.find_spec('waveshare_bridge'))

    def test_source_cli_resolves_the_dashboard_plugin(self):
        source = cli.default_plugin_source()
        self.assertEqual(source, ROOT.parent / 'plugins' / 'hermes' / 'dashboard-plugin')
        self.assertTrue((source / 'plugin.yaml').is_file())
        self.assertTrue((source / 'dashboard' / 'plugin_api.py').is_file())

    def test_bridge_descriptions_are_host_generic_outside_hermes_specific_features(self):
        cli_source = (ROOT / 'waveshare_bridge' / 'cli.py').read_text()
        config_source = (ROOT / 'waveshare_bridge' / 'config.py').read_text()
        enroll_source = (ROOT / 'waveshare_bridge' / 'enroll.py').read_text()
        project = tomllib.loads((ROOT / 'pyproject.toml').read_text())
        self.assertNotIn('on the Mac that runs Hermes', cli_source)
        self.assertNotIn('THIS Mac', config_source)
        self.assertNotIn('on this Mac', config_source)
        self.assertNotIn('the Mac.', enroll_source)
        self.assertNotIn('Connect to Hermes', enroll_source)
        self.assertNotIn('same Mac', project['project']['description'])
        self.assertIn('provider hosts', project['project']['description'])


if __name__ == '__main__':
    unittest.main()
