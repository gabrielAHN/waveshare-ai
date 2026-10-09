"""Source-level acceptance checks for the single supported firmware contract."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"


class CurrentContract(unittest.TestCase):
    def test_gateway_is_the_only_voice_transport(self):
        worker = (ROOT / "plugins/hermes/firmware/helper_voice.c").read_text()
        wire = (ROOT / "plugins/hermes/firmware/gadget_wire.h").read_text()
        config = (FIRMWARE / "main/Kconfig.projbuild").read_text()
        for token in ("send_command_http", "/v1/voice", "/v1/command", "CONFIG_WAVESHARE_AI_VOICE_GATEWAY"):
            self.assertNotIn(token, worker)
        self.assertNotIn("config WAVESHARE_AI_VOICE_GATEWAY", config)
        for token in ("gw_parse_command", "GW_PORT 8767", "gw_url_from_base"):
            self.assertNotIn(token, wire)
        self.assertIn("send_command_gateway", worker)
        self.assertIn("config WAVESHARE_AI_GATEWAY_PORT", config)

    def test_unconsumed_quota_surface_is_absent(self):
        self.assertFalse((FIRMWARE / "main/quota_view.h").exists())
        for path in ("plugins/hermes/firmware/bots_view.h", "plugins/home_assistant/firmware/sensors_view.h"):
            self.assertNotIn("quota_view.h", (ROOT / path).read_text())

    def test_build_uses_current_device_keys_only(self):
        self.assertNotIn("local_config_renamed", (ROOT / "tools/local_config.sh").read_text())
        self.assertNotIn("PLUGINS", (ROOT / "dev.sh").read_text())

    def test_wifi_worker_has_no_setup_migration(self):
        self.assertNotIn("matter_dbg", (FIRMWARE / "main/home_wifi.c").read_text())

    def test_live_diagnostics_use_wls4_size(self):
        source = (FIRMWARE / "main/home_live.c").read_text()
        self.assertNotIn("sample.format", source)
        self.assertIn("format=WLS4", source)
        self.assertIn("sample.count*10", source)


if __name__ == "__main__":
    unittest.main()
