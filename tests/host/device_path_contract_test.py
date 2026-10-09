"""All public build/read helpers use the single named-device firmware tree."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
DEVICE_REL = "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
FIRMWARE = ROOT / DEVICE_REL


class DevicePathContract(unittest.TestCase):
    def test_root_entry_points_name_the_device_firmware(self):
        for relative in (
            "dev.sh",
            "tools/flash.sh",
            "tools/package-release.sh",
            "tools/demo_video.sh",
            "tools/render_docs_previews.sh",
            "tools/bot_preview.sh",
            "tests/run_host_tests.sh",
            "tests/perf/run_home_renderer_bench.sh",
        ):
            with self.subTest(path=relative):
                source = (ROOT / relative).read_text()
                self.assertIn(DEVICE_REL, source)
                self.assertNotIn('$ROOT/firmware', source)
                self.assertNotIn('$SOURCE_ROOT/firmware', source)

    def test_python_path_readers_name_the_device_firmware(self):
        for relative in (
            "tests/project_identity_test.py",
            "tests/host/current_contract_test.py",
            "tests/host/firmware_identity_test.py",
            "tests/host/helper_ease_contract_test.py",
            "tests/preview/demo_output_test.py",
            "tests/preview/demo_public_art_test.py",
            "tests/preview/docs_layout_test.py",
            "tests/preview/generate_home_font.py",
            "tests/preview/generate_tile_art.py",
            "tests/preview/motion_output_test.py",
            "tools/generate_material_symbols.py",
        ):
            with self.subTest(path=relative):
                self.assertIn(DEVICE_REL, (ROOT / relative).read_text())

    def test_component_reaches_shared_plugins_from_new_depth(self):
        component = (FIRMWARE / "main" / "CMakeLists.txt").read_text()
        self.assertIn('${CMAKE_CURRENT_LIST_DIR}/../../../../plugins', component)
        self.assertIn('"-fmacro-prefix-map=${waveshare_PLUGINS_DIR}=../../../../plugins"', component)

    def test_generated_and_synthetic_build_paths_follow_device_layout(self):
        ignore = (ROOT / ".gitignore").read_text()
        self.assertIn(DEVICE_REL + "/build*", ignore)
        self.assertIn(DEVICE_REL + "/sdkconfig", ignore)
        self.assertNotIn("\nfirmware/build", ignore)
        bounded = (ROOT / "bridge/tests/test_bounded_tools.py").read_text()
        self.assertIn("DEVICE_PARTS", bounded)
        self.assertIn(DEVICE_REL, bounded)


if __name__ == "__main__":
    unittest.main()
