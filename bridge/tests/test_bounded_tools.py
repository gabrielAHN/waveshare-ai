"""Functional checks for the two release/USB tools missed by the naming migration."""
import contextlib
import importlib.util
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEVICE_REL = pathlib.Path('devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware')
DEVICE_PARTS = DEVICE_REL.parts


def load_pair_usb():
    spec = importlib.util.spec_from_file_location('bounded_pair_usb', ROOT / 'tools' / 'pair_usb.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleaseToolTests(unittest.TestCase):
    def test_release_package_uses_current_names_with_synthetic_firmware(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = pathlib.Path(tmp)
            (fixture / 'tools').mkdir()
            build = fixture.joinpath(*DEVICE_PARTS, 'build')
            (build / 'bootloader').mkdir(parents=True)
            shutil.copy2(ROOT / 'tools' / 'package-release.sh', fixture / 'tools' / 'package-release.sh')
            (build / 'bootloader' / 'bootloader.bin').write_bytes(b'boot-fixture')
            (build / 'waveshare_ai.bin').write_bytes(b'app-fixture')
            (build / 'flash_args').write_text(
                '--flash-mode dio --flash-freq 80m --flash-size 16MB\n'
                '0x0 bootloader/bootloader.bin\n0x10000 waveshare_ai.bin\n')
            env = {**os.environ, 'VERSION': 'fixture'}
            run = subprocess.run(['/bin/bash', str(fixture / 'tools' / 'package-release.sh')],
                                 cwd=fixture, env=env, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            folder = fixture / 'dist' / 'waveshare-ai-fixture'
            archive = fixture / 'dist' / 'waveshare-ai-fixture.zip'
            self.assertTrue((folder / 'waveshare_ai.bin').is_file())
            self.assertTrue(archive.is_file())
            self.assertEqual(run.stdout.splitlines()[-2:], [str(folder), str(archive)])
            with zipfile.ZipFile(archive) as packaged:
                self.assertTrue(all(name.startswith('waveshare-ai-fixture/') for name in packaged.namelist()))


class PairUsbToolTests(unittest.TestCase):
    def test_cli_help_default_name_and_frame_are_current_without_serial(self):
        module = load_pair_usb()
        captured = {}
        real_frame = module.bridge_frame

        def observe(base, fingerprint, name):
            captured['name'] = name
            captured['frame'] = real_frame(base, fingerprint, name)
            return captured['frame']

        argv = ['pair_usb.py', '--dry-run', 'bridge', 'https://10.99.0.8:8098', 'ab' * 32]
        out = io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'bridge_frame', side_effect=observe), \
                contextlib.redirect_stdout(out):
            self.assertEqual(module.main(), 0)
        self.assertEqual(captured['name'], 'Waveshare AI bridge')
        self.assertEqual(captured['frame'][:4], b'WLB2')
        self.assertIn('WLB2', out.getvalue())
        self.assertIn('waveshare-bridge enroll', module.__doc__)
        help_out = io.StringIO()
        with mock.patch.object(sys, 'argv', ['pair_usb.py', '--help']), contextlib.redirect_stdout(help_out), \
                self.assertRaises(SystemExit) as stopped:
            module.main()
        self.assertEqual(stopped.exception.code, 0)
        self.assertIn('waveshare-bridge enroll', help_out.getvalue())


if __name__ == '__main__':
    unittest.main()
