"""Firmware-owned Waveshare AI identity and connection-language contracts."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"


class FirmwareIdentity(unittest.TestCase):
    def test_build_and_local_config_use_waveshare_identity(self):
        project = (FIRMWARE / "CMakeLists.txt").read_text()
        component = (FIRMWARE / "main/CMakeLists.txt").read_text()
        kconfig = (FIRMWARE / "main/Kconfig.projbuild").read_text()
        local_config = (ROOT / "tools/local_config.sh").read_text()
        env_example = (ROOT / ".env.example").read_text()
        self.assertRegex(project, r"(?m)^project\(waveshare_ai\)$")
        self.assertIn("CONFIG_WAVESHARE_AI_PROVIDER_HERMES", component)
        self.assertIn('menu "Waveshare AI providers"', kconfig)
        self.assertIn("WAVESHARE_AI_PROVIDERS", local_config)
        self.assertIn("WAVESHARE_AI_WIFI_SSID=", env_example)
        config_refs = re.findall(r"\bCONFIG_([A-Z][A-Z0-9_]*)", component)
        config_defs = re.findall(r"(?m)^\s*config ([A-Z][A-Z0-9_]*)", kconfig)
        env_keys = re.findall(r"(?m)^([A-Z][A-Z0-9_]*)=", env_example)
        self.assertTrue(config_refs and all(name.startswith("WAVESHARE_AI_") for name in config_refs))
        self.assertTrue(config_defs and all(name.startswith("WAVESHARE_AI_") for name in config_defs))
        self.assertTrue(env_keys and all(name.startswith("WAVESHARE_AI_") for name in env_keys))

    def test_enrollment_discovery_and_sdk_identity_are_current(self):
        pair = (FIRMWARE / "main/pair_state.h").read_text()
        discovery = (FIRMWARE / "main/home_pair.c").read_text()
        gadget = (ROOT / "plugins/hermes/firmware/gadget_wire.h").read_text()
        self.assertIn('"waveshare-ai-enroll-v1"', pair)
        self.assertIn('"waveshare-ai-%02x%02x"', discovery)
        self.assertIn('mdns_query_ptr("_waveshare-ai", "_tcp"', discovery)
        self.assertIn('#define GW_KEY_CONTEXT "waveshare-ai-gadget-key-v1"', gadget)
        self.assertIn(r'\"firmware\":\"waveshare-ai\"', gadget)

    def test_connection_ui_is_host_generic(self):
        sources = [
            FIRMWARE / "main/home_render.h",
            FIRMWARE / "main/home_pair.c",
            ROOT / "plugins/hermes/firmware/bots_view.h",
            ROOT / "plugins/home_assistant/firmware/sensors_view.h",
        ]
        rendered = "\n".join(path.read_text() for path in sources)
        self.assertIn("Looking for your bridge", rendered)
        self.assertIn("Bridge not found", rendered)
        self.assertIn("Set up on the host", rendered)
        self.assertNotIn("Hermes not found", rendered)
        home_ui = (FIRMWARE / "main/home_ui.h").read_text()
        self.assertIn('r.label="Set up bridge"', home_ui)
        self.assertNotIn('r.label="Set up Hermes"', home_ui)
        self.assertNotRegex(rendered, re.compile(r"\bMac\b"))

    def test_static_component_composition_is_build_gated(self):
        component = (FIRMWARE / "main/CMakeLists.txt").read_text()
        owner = (FIRMWARE / "main/direct_main.c").read_text()
        self.assertIn('set(owner_source "direct_main.c" "home_wifi.c" "pmu_diag.c" "power_pmu.c" "power_main.c")', component)
        self.assertRegex(
            component,
            r'if\(waveshare_PROVIDER_HERMES OR waveshare_PROVIDER_HOME_ASSISTANT\)\s+'
            r'list\(APPEND owner_source "home_live\.c" "home_pair\.c" "pair_tls\.c"\)',
        )
        self.assertRegex(
            component,
            r'if\(waveshare_PLUGIN_AI\)\s+'
            r'list\(APPEND owner_source "\$\{waveshare_PLUGINS_DIR\}/hermes/firmware/helper_voice\.c"\)',
        )
        self.assertIn("#if WAVESHARE_AI_PLUGIN_BRIDGE\n home_live_start", owner)
        self.assertIn("#if WAVESHARE_AI_PLUGIN_AI\n helper_voice_start", owner)
        plugin_sources = sorted(
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "plugins").glob("*/firmware/*.c")
        )
        self.assertEqual(plugin_sources, ["plugins/hermes/firmware/helper_voice.c"])


if __name__ == "__main__":
    unittest.main()
