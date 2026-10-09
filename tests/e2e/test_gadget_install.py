"""Exercise the real plugin installer in disposable directories, never the live plugin."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PluginInstallTests(unittest.TestCase):
    def test_install_preserves_existing_settings_and_verified_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = root / 'plugin'
            target.mkdir()
            original = {
                'issuer': 'https://auth.example.com',
                'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions',
                'client_id': 'waveshare-sessions',
                'scope': 'hermes.sessions.read',
                'command_scope': 'hermes.helper.command',
                'profile': 'helper',
                'command_profiles': ['helper', 'atlas', 'coding'],
                'operator_setting': {'retain': True},
            }
            settings = target / 'settings.json'
            settings.write_text(json.dumps(original))
            settings.chmod(0o600)
            registry = root / 'config' / 'boards.json'
            result = subprocess.run(
                ['bash', str(ROOT / 'tools/install-live-plugin.sh'), '--target', str(target),
                 '--backup-dir', str(root / 'backups')],
                env={**os.environ, 'HOME': str(root)}, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('hermes pm install python', result.stdout)
            actual = json.loads(settings.read_text())
            for key, value in original.items():
                self.assertEqual(actual[key], value, key)
            self.assertNotIn('gadget', actual)
            backups = list((root / 'backups').glob('*/settings.json'))
            self.assertEqual(len(backups), 1)
            self.assertEqual(json.loads(backups[0].read_text()), original)
            self.assertEqual(settings.stat().st_mode & 0o777, 0o600)


    def test_requested_profiles_reject_oversized_ids_even_in_dry_run(self):
        for profile in ('abcdefghijk1', 'abcdefghijk2', 'abcdefghijk1,abcdefghijk2'):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                target = root / 'plugin'
                target.mkdir()
                settings = target / 'settings.json'
                settings.write_text(json.dumps({'keep': 'unchanged'}))
                before = settings.read_bytes()
                result = subprocess.run(
                    ['bash', str(ROOT / 'tools/install-live-plugin.sh'), '--target', str(target),
                     '--backup-dir', str(root / 'backups'), '--profiles', profile, '--dry-run'],
                    env={**os.environ, 'HOME': tmp}, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('invalid --profiles', result.stderr)
                self.assertEqual(settings.read_bytes(), before)
                self.assertFalse((root / 'backups').exists())

    def test_requested_maximum_profile_is_installed_literally(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = root / 'plugin'
            target.mkdir()
            settings = target / 'settings.json'
            settings.write_text(json.dumps({'issuer': 'https://auth.example.com',
                'audience': 'https://hermes.example.com', 'client_id': 'waveshare-sessions',
                'scope': 'hermes.sessions.read'}))
            result = subprocess.run(
                ['bash', str(ROOT / 'tools/install-live-plugin.sh'), '--target', str(target),
                 '--backup-dir', str(root / 'backups'), '--profiles', 'abcdefghijk'],
                env={**os.environ, 'HOME': tmp}, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(settings.read_text())['command_profiles'], ['abcdefghijk'])

    def test_dry_run_does_not_touch_target_or_create_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = root / 'plugin'
            target.mkdir()
            original = {'keep': 'unchanged'}
            settings = target / 'settings.json'
            settings.write_text(json.dumps(original))
            result = subprocess.run(
                ['bash', str(ROOT / 'tools/install-live-plugin.sh'), '--target', str(target),
                 '--backup-dir', str(root / 'backups'), '--dry-run'],
                env={**os.environ, 'HOME': str(root)}, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(settings.read_text()), original)
            self.assertFalse((root / 'backups').exists())


    def test_invalid_capability_candidate_is_rejected_before_any_target_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = root / 'plugin'
            target.mkdir()
            original = {
                'issuer': 'https://auth.example.com',
                'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions',
                'client_id': 'waveshare-sessions', 'scope': 'hermes.sessions.read',
                'command_scope': 'hermes.helper.command', 'profile': 'helper',
                'command_profiles': ['default'],
            }
            settings = target / 'settings.json'
            settings.write_text(json.dumps(original))
            before = settings.read_bytes()
            marker = target / 'core.py'
            marker.write_text('original plugin code\n')
            result = subprocess.run(
                ['bash', str(ROOT / 'tools/install-live-plugin.sh'), '--target', str(target),
                 '--backup-dir', str(root / 'backups')],
                env={**os.environ, 'HOME': str(root)}, capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(settings.read_bytes(), before)
            self.assertEqual(marker.read_text(), 'original plugin code\n')
            self.assertFalse((root / 'backups').exists())


class InstalledPluginTests(unittest.TestCase):
    def test_cache_only_source_vendor_is_not_installed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'source'
            plugin = source / 'plugins/hermes/dashboard-plugin'
            shutil.copytree(ROOT / 'plugins/hermes/dashboard-plugin', plugin,
                            ignore=shutil.ignore_patterns('vendor', '__pycache__', 'tests'))
            (source / 'tools').mkdir()
            installer = source / 'tools/install-live-plugin.sh'
            shutil.copy2(ROOT / 'tools/install-live-plugin.sh', installer)
            cache = plugin / 'vendor/hermes_gadget_plugin/__pycache__/hub.cpython-311.pyc'
            cache.parent.mkdir(parents=True)
            cache.write_bytes(b'synthetic ignored bytecode')
            self.assertEqual(list((plugin / 'vendor').rglob('*.pyc')), [cache])
            self.assertFalse(list((plugin / 'vendor').rglob('*.py')))
            target = root / 'plugin'
            target.mkdir()
            settings = {'issuer': 'https://auth.example.com', 'audience': 'https://hermes.example.com',
                        'client_id': 'waveshare-sessions', 'scope': 'hermes.sessions.read',
                        'command_scope': 'hermes.helper.command', 'operator_key': {'retain': True}}
            (target / 'settings.json').write_text(json.dumps(settings))
            unknown = target / 'operator.txt'
            unknown.write_text('keep unknown user file\n')
            result = subprocess.run(['bash', str(installer), '--target', str(target),
                                     '--backup-dir', str(root / 'backups')],
                                    env={**os.environ, 'HOME': tmp}, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(unknown.read_text(), 'keep unknown user file\n')
            self.assertEqual((target / 'core.py').read_bytes(), (plugin / 'core.py').read_bytes())
            self.assertEqual(json.loads((target / 'settings.json').read_text())['operator_key'],
                             settings['operator_key'])
            self.assertEqual(len(list((root / 'backups').glob('*/settings.json'))), 1)
            self.assertFalse((target / 'vendor').exists(), 'excluded cache must not leave a vendor hierarchy')
            # A subsequent install must not remove an operator-owned file in that path.
            retained = target / 'vendor/operator.txt'
            retained.parent.mkdir()
            retained.write_text('not installer-owned\n')
            result = subprocess.run(['bash', str(installer), '--target', str(target),
                                     '--backup-dir', str(root / 'second-backups')],
                                    env={**os.environ, 'HOME': tmp}, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(retained.read_text(), 'not installer-owned\n')

    def test_installed_core_imports_without_sdk_and_keeps_operator_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'plugin'
            target.mkdir()
            original = {'issuer': 'https://auth.example.com',
                        'audience': 'https://hermes.example.com/api/plugins/waveshare-sessions',
                        'client_id': 'waveshare-sessions', 'scope': 'hermes.sessions.read',
                        'command_scope': 'hermes.helper.command', 'profile': 'helper',
                        'gadget': {'enabled': True}, 'operator_key': {'retained': True}}
            (target / 'settings.json').write_text(json.dumps(original))
            result = subprocess.run(['bash', str(ROOT / 'tools/install-live-plugin.sh'), '--target', str(target),
                    '--backup-dir', str(Path(tmp) / 'backups')], env={**os.environ, 'HOME': tmp},
                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            settings = json.loads((target / 'settings.json').read_text())
            self.assertTrue(all(settings[k] == v for k, v in original.items()))
            self.assertFalse((target / 'vendor').exists())
            self.assertFalse((target / 'helper.py').exists())
            probe = ('import importlib.util,sys,pathlib; '
                     'spec=importlib.util.spec_from_file_location("installed_core", sys.argv[1]); '
                     'core=importlib.util.module_from_spec(spec); spec.loader.exec_module(core); '
                     'settings=core.load_settings(pathlib.Path(sys.argv[2])); '
                     'sys.exit(0 if "gadget" not in settings and "profile" not in settings else 1)')
            for options in ([], ['-O']):
                run = subprocess.run([sys.executable, '-S', *options, '-c', probe,
                                      str(target / 'core.py'), str(target / 'settings.json')],
                                     capture_output=True, text=True, cwd=tmp)
                self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
