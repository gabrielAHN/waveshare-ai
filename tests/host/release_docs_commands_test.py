"""Execute documented release commands against isolated path-only stubs."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
DEVICE_README = ROOT / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/README.md"
PERF_README = ROOT / "tests/perf/README.md"
CANDIDATE_BENCH_HELPER = ROOT / "tests/perf/run_home_renderer_bench.sh"
LEGACY_BENCH_FIXTURE = ROOT / "tests/fixtures/perf/pre-layout-run-home-renderer-bench.sh"
LEGACY_BENCH_SHA256 = "0cc64ceb7148cf5a60542aa39a888de741972ac838e26fd516c0410032ef79a3"


class DeviceDocumentationCommands(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ["TMPDIR"])
        self.case = Path(tempfile.mkdtemp(prefix="repair1-wifi-", dir=scratch))

    def documented_fragment(self, readme=None):
        readme = DEVICE_README if readme is None else readme
        lines = readme.read_text().splitlines()
        heading = lines.index("## Wi-Fi and flashing")
        section_end = next(
            (index for index in range(heading + 1, len(lines)) if lines[index].startswith("#")),
            len(lines),
        )
        section = lines[heading + 1 : section_end]
        fence = section.index("```sh")
        end = section.index("```", fence + 1)
        block = section[fence + 1 : end]

        selected = []
        source_count = 0
        update_count = 0
        for line in block:
            command = line.strip()
            if not command:
                continue
            if "tools/flash.sh" in command or "tools/set_wifi.py --forget" in command:
                continue
            if command.startswith("export IDF_PATH="):
                selected.append(line)
            elif command.startswith(". ") and "export.sh" in command:
                selected.append(line)
                source_count += 1
            elif command.startswith("python tools/set_wifi.py") and "--env" in command:
                selected.append(line)
                update_count += 1

        if source_count != 1 or update_count != 1:
            raise ValueError("Wi-Fi fenced block must contain one SDK source and one --env update")
        return "\n".join(selected)

    def make_sdk(self, path):
        stub_bin = path / "stub-bin"
        stub_bin.mkdir(parents=True)
        (path / "export.sh").write_text(
            'export DOC_TEST_SDK="$IDF_PATH"\n'
            'export PATH="$IDF_PATH/stub-bin:$PATH"\n'
        )
        python = stub_bin / "python"
        python.write_text(
            '#!/bin/sh\n'
            'printf "%s\\n" "$DOC_TEST_SDK" > "$DOC_TEST_LOG"\n'
            'printf "<%s>\\n" "$@" >> "$DOC_TEST_LOG"\n'
        )
        python.chmod(0o755)

    def run_fragment(self, checkout, sdk=None, readme=None):
        checkout.mkdir(parents=True)
        (checkout / ".env").write_text("WAVESHARE_AI_WIFI_SSID=Example\n")
        log = checkout / "wifi-stub.log"
        env = {
            "HOME": os.environ["HOME"],
            "HERMES_HOME": os.environ["HERMES_HOME"],
            "TMPDIR": os.environ["TMPDIR"],
            "PATH": "/usr/bin:/bin",
            "DOC_TEST_LOG": str(log),
        }
        if sdk is not None:
            env["IDF_PATH"] = str(sdk)
        run = subprocess.run(
            ["/bin/bash", "-eu", "-c", self.documented_fragment(readme)],
            cwd=checkout,
            env=env,
            text=True,
            capture_output=True,
        )
        return run, log

    def test_documented_update_argument_is_execution_authority(self):
        checkout = self.case / "repo"
        sdk = self.case / "custom-idf"
        self.make_sdk(sdk)
        readme = self.case / "README.md"
        source = DEVICE_README.read_text().replace(
            "python tools/set_wifi.py --env .env",
            "python tools/set_wifi.py --env mutated.env",
        )
        readme.write_text(source)
        run, log = self.run_fragment(checkout, sdk, readme)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(
            log.read_text().splitlines()[1:],
            ["<tools/set_wifi.py>", "<--env>", "<mutated.env>"],
        )

    def test_missing_wifi_fence_rejects_later_foreign_block_before_execution(self):
        readme = self.case / "missing-wifi-fence.md"
        readme.write_text(
            "# Device\n\n"
            "## Wi-Fi and flashing\n\n"
            "No command block is present here.\n\n"
            "## Unrelated later section\n\n"
            "```sh\n"
            'export IDF_PATH="${IDF_PATH:-$PWD/../esp-idf-6.0.2}"\n'
            '. "$IDF_PATH/export.sh"\n'
            "python tools/set_wifi.py --env .env\n"
            "```\n"
        )
        with mock.patch.object(subprocess, "run") as run:
            with self.assertRaises(ValueError):
                self.run_fragment(self.case / "repo", readme=readme)
        run.assert_not_called()

    def test_unclosed_wifi_fence_rejects_later_foreign_block_before_execution(self):
        readme = self.case / "unclosed-wifi-fence.md"
        readme.write_text(
            "# Device\n\n"
            "## Wi-Fi and flashing\n\n"
            "```sh\n"
            "# deliberately unclosed local fence\n\n"
            "## Unrelated later section\n\n"
            "```sh\n"
            'export IDF_PATH="${IDF_PATH:-$PWD/../esp-idf-6.0.2}"\n'
            '. "$IDF_PATH/export.sh"\n'
            "python tools/set_wifi.py --env .env\n"
            "```\n"
        )
        with mock.patch.object(subprocess, "run") as run:
            with self.assertRaises(ValueError):
                self.run_fragment(self.case / "repo", readme=readme)
        run.assert_not_called()

    def test_missing_wifi_fence_without_later_block_rejects_before_execution(self):
        readme = self.case / "no-wifi-fence.md"
        readme.write_text("# Device\n\n## Wi-Fi and flashing\n\nNo command block.\n")
        with mock.patch.object(subprocess, "run") as run:
            with self.assertRaises(ValueError):
                self.run_fragment(self.case / "repo", readme=readme)
        run.assert_not_called()

    def test_valid_wifi_fence_ignores_later_foreign_block(self):
        readme = self.case / "valid-wifi-fence.md"
        readme.write_text(
            "# Device\n\n"
            "## Wi-Fi and flashing\n\n"
            "```sh\n"
            'export IDF_PATH="${IDF_PATH:-$PWD/../esp-idf-6.0.2}"\n'
            '. "$IDF_PATH/export.sh"\n'
            "python tools/set_wifi.py --env .env\n"
            "```\n\n"
            "## Unrelated later section\n\n"
            "```sh\n"
            "python tools/set_wifi.py --env foreign.env\n"
            "```\n"
        )
        checkout = self.case / "repo"
        sdk = self.case / "custom-idf"
        self.make_sdk(sdk)
        run, log = self.run_fragment(checkout, sdk, readme)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assert_stub_call(log, sdk)

    def assert_stub_call(self, log, sdk, default=False):
        lines = log.read_text().splitlines()
        self.assertEqual(lines[1:], ["<tools/set_wifi.py>", "<--env>", "<.env>"])
        if default:
            self.assertEqual(Path(lines[0]).resolve(), sdk.resolve())
        else:
            self.assertEqual(lines[0], str(sdk))

    def test_unset_idf_path_uses_documented_root_default(self):
        checkout = self.case / "repo"
        sdk = self.case / "esp-idf-6.0.2"
        self.make_sdk(sdk)
        run, log = self.run_fragment(checkout)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assert_stub_call(log, sdk, default=True)

    def test_explicit_idf_path_is_preserved(self):
        checkout = self.case / "repo"
        sdk = self.case / "custom-idf"
        self.make_sdk(sdk)
        run, log = self.run_fragment(checkout, sdk)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assert_stub_call(log, sdk)

    def test_spaces_in_checkout_and_sdk_paths_are_preserved(self):
        checkout = self.case / "checkout with spaces" / "repo"
        sdk = self.case / "custom SDK with spaces"
        self.make_sdk(sdk)
        run, log = self.run_fragment(checkout, sdk)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assert_stub_call(log, sdk)

    def test_missing_default_stops_before_wifi_command(self):
        checkout = self.case / "missing-sdk-parent" / "repo"
        run, log = self.run_fragment(checkout)
        self.assertNotEqual(run.returncode, 0)
        self.assertFalse(log.exists(), "Wi-Fi stub must not run when the SDK export is missing")


class PerformanceDocumentationCommands(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ["TMPDIR"])
        self.case = Path(tempfile.mkdtemp(prefix="repair1-perf-", dir=scratch))
        self.compiler = self.case / "compiler-path-stub"
        self.compiler.write_text(
            '#!/bin/sh\n'
            ': > "$DOC_COMPILER_LOG"\n'
            'missing=0\n'
            'for arg do\n'
            '  case "$arg" in\n'
            '    -I*) path=${arg#-I} ;;\n'
            '    *.c) path=$arg ;;\n'
            '    *) continue ;;\n'
            '  esac\n'
            '  printf "%s\\n" "$path" >> "$DOC_COMPILER_LOG"\n'
            '  [ -e "$path" ] || missing=1\n'
            'done\n'
            '[ "$missing" -eq 0 ] && exit 77\n'
            'exit 78\n'
        )
        self.compiler.chmod(0o755)

    def run_helper(self, helper, source_root, label):
        log = self.case / (label + ".log")
        out = self.case / (label + ".out")
        env = {
            "HOME": os.environ["HOME"],
            "HERMES_HOME": os.environ["HERMES_HOME"],
            "TMPDIR": os.environ["TMPDIR"],
            "PATH": "/usr/bin:/bin",
            "CC": str(self.compiler),
            "DOC_COMPILER_LOG": str(log),
        }
        run = subprocess.run(
            ["/bin/bash", str(helper), str(source_root), str(out)],
            cwd=ROOT,
            env=env,
            text=True,
            capture_output=True,
        )
        return run, log.read_text().splitlines()

    def make_legacy_reference(self):
        fixture_sha = hashlib.sha256(LEGACY_BENCH_FIXTURE.read_bytes()).hexdigest()
        self.assertEqual(fixture_sha, LEGACY_BENCH_SHA256)
        root = self.case / "retained pre-relocation tree"
        for relative in (
            "firmware/main",
            "firmware/components/qrcodegen",
            "plugins/hermes/firmware",
            "plugins/home_assistant/firmware",
            "tests/perf",
        ):
            (root / relative).mkdir(parents=True)
        (root / "tests/perf/home_renderer_bench.c").write_bytes(b"")
        (root / "firmware/components/qrcodegen/qrcodegen.c").write_bytes(b"")
        helper = root / "tests/perf/run_home_renderer_bench.sh"
        shutil.copyfile(LEGACY_BENCH_FIXTURE, helper)
        shutil.copymode(LEGACY_BENCH_FIXTURE, helper)
        return root, helper

    def test_reference_fixture_is_exact_immutable_helper(self):
        actual = hashlib.sha256(LEGACY_BENCH_FIXTURE.read_bytes()).hexdigest()
        self.assertEqual(actual, LEGACY_BENCH_SHA256)

    def test_readme_uses_layout_matched_helpers_and_projects(self):
        source = PERF_README.read_text()
        self.assertNotIn("using this candidate fixture against either source tree", source)
        self.assertNotIn("/path/to/immutable-b589", source)
        self.assertIn("requires the named-device layout", source)
        self.assertIn('tests/perf/run_home_renderer_bench.sh "$PWD"', source)
        self.assertIn(
            "/path/to/retained-pre-relocation-tree/tests/perf/run_home_renderer_bench.sh",
            source,
        )
        self.assertIn("-C /path/to/retained-pre-relocation-tree/tests/perf", source)
        self.assertIn("path controls, not benchmark results", source)

    def test_current_helper_accepts_current_named_device_layout(self):
        run, paths = self.run_helper(CANDIDATE_BENCH_HELPER, ROOT, "current-helper-current-layout")
        self.assertEqual(run.returncode, 77, run.stderr)
        self.assertTrue(paths)
        self.assertTrue(all(Path(path).exists() for path in paths))
        self.assertTrue(any("devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main" in path for path in paths))

    def test_retained_reference_uses_its_own_helper(self):
        legacy, own_helper = self.make_legacy_reference()
        run, paths = self.run_helper(own_helper, legacy, "legacy-helper-legacy-layout")
        self.assertEqual(run.returncode, 77, run.stderr)
        self.assertTrue(paths)
        self.assertTrue(all(Path(path).exists() for path in paths))
        self.assertIn(str(legacy / "firmware/main"), paths)
        self.assertFalse(any("devices/waveshare-esp32-s3-touch-amoled-1-8-v2" in path for path in paths))

    def test_candidate_helper_rejects_legacy_layout_as_expected_diagnostic(self):
        legacy, _ = self.make_legacy_reference()
        run, paths = self.run_helper(CANDIDATE_BENCH_HELPER, legacy, "candidate-helper-legacy-layout")
        self.assertEqual(run.returncode, 78, run.stderr)
        self.assertTrue(any(not Path(path).exists() for path in paths))



if __name__ == "__main__":
    unittest.main()
