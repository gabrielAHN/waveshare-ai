"""Public docs previews have one owner and include the core Display screen."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
CORE = {"home-settings", "page-settings", "settings-battery-charging", "settings-battery-on-battery", "settings-display"}
HERMES = {"home-sparkles", "home-ask", "sparkles", "sparkles-sunset", "ask-idle-helper", "ask-idle-coding", "ask-idle-atlas", "ask-listening", "ask-running", "ask-done", "ask-done-dark", "ask-error", "settings-hermes", "settings-sound"}
HA = {"home-sensor", "home-sensor-sign-in", "sensor", "sensor-dark", "settings-home-assistant", "settings-shared"}


class DocsPreviews(unittest.TestCase):
    def test_public_frame_manifest(self):
        with tempfile.TemporaryDirectory(prefix="docs-layout-", dir=os.environ.get("TMPDIR")) as temporary:
            out = Path(temporary)
            binary = out / "preview"
            subprocess.run([os.environ.get("CC", "cc"), "-O2", "-Wall", "-Wextra", "-Werror", "-DBOT_NO_LOCAL_OUTFITS=1",
                            *("-I" + str(p) for p in (FIRMWARE / "main", ROOT / "plugins/hermes/firmware", ROOT / "plugins/home_assistant/firmware", FIRMWARE / "components/qrcodegen")),
                            str(ROOT / "tests/preview/docs_preview.c"), str(FIRMWARE / "components/qrcodegen/qrcodegen.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(out)], check=True, stdout=subprocess.DEVNULL)
            expected = CORE | {"hermes--" + p for p in HERMES} | {"home_assistant--" + p for p in HA}
            self.assertEqual({p.stem for p in out.glob("*.ppm")}, expected)
            for frame in out.glob("*.ppm"):
                data = frame.read_bytes()
                self.assertTrue(data.startswith(b"P6\n368 448\n255\n"))
                self.assertEqual(len(data), len(b"P6\n368 448\n255\n") + 368 * 448 * 3)


if __name__ == "__main__":
    unittest.main()
