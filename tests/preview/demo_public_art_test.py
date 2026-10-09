#!/usr/bin/env python3
"""Public demos must ignore local outfits, even if a local header is found on the include path.

Compile with and without a sentinel private header and hash streamed raw pixels, without storing
multi-gigabyte frame sequences. Pass a fresh --out and an existing demo asset directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    local = out / "synthetic-local"
    local.mkdir()
    (local / "bot_outfits_local.h").write_text('#error "Public demo included local outfits"\n')
    env = os.environ.copy()
    env.pop("DEMO_FRAMES", None)
    results = []
    for variant in ("without-local", "with-local"):
        binary = out / variant
        command = [env.get("CC", "cc"), "-O2", "-Wall", "-Wextra", "-Werror"]
        if variant == "with-local":
            command += ["-I" + str(local)]
        for inc in (FIRMWARE / "main", ROOT / "plugins/hermes/firmware", ROOT / "plugins/home_assistant/firmware", FIRMWARE / "components/qrcodegen"):
            command += ["-I" + str(inc)]
        command += [str(ROOT / "tests/preview/demo_video.c"), str(FIRMWARE / "components/qrcodegen/qrcodegen.c"), "-lm", "-o", str(binary)]
        proc = subprocess.run(command, capture_output=True, text=True)
        (out / (variant + "-compile.log")).write_text(proc.stdout + proc.stderr)
        if proc.returncode:
            print(json.dumps({"variant": variant, "compile_rc": proc.returncode, "passed": False}))
            return 1
        run = out / (variant + "-assets")
        run.mkdir()
        for name in ("mask.pgm", *("frame-%d.ppm" % k for k in range(5))):
            (run / name).symlink_to(args.assets.resolve() / name)
        digest = hashlib.sha256()
        count = 0
        with (out / (variant + "-render.log")).open("wb") as stderr:
            proc = subprocess.Popen([str(binary), str(run)], env=env, stdout=subprocess.PIPE, stderr=stderr)
            if proc.stdout is None:
                proc.kill()
                proc.wait()
                raise RuntimeError("Renderer stdout pipe was not created")
            with proc.stdout:
                while chunk := proc.stdout.read(1024 * 1024):
                    digest.update(chunk)
                    count += len(chunk)
            rc = proc.wait()
        results.append({"variant": variant, "render_rc": rc, "bytes": count, "sha256": digest.hexdigest()})
        if rc:
            print(json.dumps(results))
            return 1
    passed = results[0]["bytes"] > 0 and results[0]["bytes"] == results[1]["bytes"] and results[0]["sha256"] == results[1]["sha256"]
    (out / "results.json").write_text(json.dumps({"passed": passed, "variants": results}, indent=2) + "\n")
    print(json.dumps({"passed": passed, "variants": results}))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
