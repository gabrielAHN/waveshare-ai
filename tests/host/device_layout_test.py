"""Parent device-first release layout acceptance (no hardware or live state)."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]
DEVICE = ROOT / "devices" / "waveshare-esp32-s3-touch-amoled-1-8-v2"
class DeviceFirstLayout(unittest.TestCase):
    def test_device_core_has_named_installable_directory(self):
        self.assertTrue((DEVICE / "firmware" / "CMakeLists.txt").is_file(), "device firmware must live under its named device folder")
        self.assertTrue((DEVICE / "firmware" / "main" / "home_ui.h").is_file())
        self.assertTrue((DEVICE / "README.md").is_file())
        self.assertFalse((ROOT / "firmware").exists(), "do not retain a root firmware directory or compatibility symlink")
    def test_optional_plugins_remain_shared_and_documented(self):
        for name in ("hermes", "home_assistant"):
            self.assertTrue((ROOT / "plugins" / name / "README.md").is_file())
            self.assertTrue((ROOT / "plugins" / name / "firmware").is_dir())
if __name__ == "__main__":
    unittest.main()
