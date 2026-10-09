"""Keep the public device, package and documented product identity aligned."""
from pathlib import Path
import re
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / 'devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware'


class ProjectIdentityTest(unittest.TestCase):
    def test_current_product_identity(self):
        package = tomllib.loads((ROOT / 'bridge/pyproject.toml').read_text())
        cmake = (FIRMWARE / 'CMakeLists.txt').read_text()
        match = re.search(r'project\(([^)]+)\)', cmake)
        actual = {
            'distribution': package['project']['name'],
            'entrypoint': package['project']['scripts'].get('waveshare-bridge'),
            'package': package['tool']['setuptools']['packages'],
            'firmware': match.group(1).strip() if match else None,
            'readme': (ROOT / 'README.md').read_text().splitlines()[0],
        }
        self.assertEqual(actual, {
            'distribution': 'waveshare-bridge',
            'entrypoint': 'waveshare_bridge.cli:main',
            'package': ['waveshare_bridge'],
            'firmware': 'waveshare_ai',
            'readme': '# Waveshare AI',
        })


if __name__ == '__main__':
    unittest.main()
