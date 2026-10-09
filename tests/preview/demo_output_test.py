#!/usr/bin/env python3
"""Real demo output regressions. Supply a fresh --out under scratch; no device/private inputs.

Sparse, blank PNM fixtures have the public layout dimensions; the entire actual storyboard runs.
RLIMIT_FSIZE=0 + ignored SIGXFSZ exposes buffered-write failures independently for each sink.
The companion C wrapper injects flush/close-only errors after real stdio calls succeed.
"""
import argparse
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"


def limited():
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = out / "demo_io_faults"
    command = [os.environ.get("CC", "cc"), "-O2", "-Wall", "-Wextra", "-Werror"]
    for inc in (FIRMWARE / "main", ROOT / "plugins/hermes/firmware", ROOT / "plugins/home_assistant/firmware", FIRMWARE / "components/qrcodegen"):
        command += ["-I" + str(inc)]
    command += [str(ROOT / "tests/preview/demo_io_faults.c"), str(FIRMWARE / "components/qrcodegen/qrcodegen.c"), "-lm", "-o", str(binary)]
    subprocess.run(command, check=True)
    fixtures = out / "fixtures"
    fixtures.mkdir()
    for name, magic, w, h, channels in [(f"frame-{k}.ppm", "P6", 1080, 1080, 3) for k in range(5)] + [("mask.pgm", "P5", 736, 896, 1)]:
        with (fixtures / name).open("wb") as f:
            header = f"{magic}\n{w} {h}\n255\n".encode()
            f.write(header)
            f.truncate(len(header) + w * h * channels)
    cases = [("limit-layout", "stdout", "limit", True), ("limit-stream", "stdout", "limit", False)]
    for sink in ("frame", "storyboard", "gif"):
        cases.append(("limit-" + sink, sink, "limit", False))
    for op in ("flush", "close"):
        for sink in ("stdout", "frame", "storyboard", "gif"):
            cases.append((op + "-" + sink, sink, op, sink == "stdout"))
    cases.append(("success", None, None, False))
    results = []
    for name, sink, fault, layout in cases:
        case = out / name
        case.mkdir()
        for fixture in fixtures.iterdir():
            (case / fixture.name).symlink_to(fixture)
        for filename, key in (("storyboard.txt", "storyboard"), ("gif.txt", "gif")):
            if sink != key:
                (case / filename).symlink_to("/dev/null")
        env = os.environ.copy()
        env.pop("DEMO_FRAMES", None)
        env.pop("DEMO_IO_FAULT", None)
        if sink == "frame":
            frames = case / "frames"
            frames.mkdir()
            env["DEMO_FRAMES"] = str(frames)
        if fault in ("close", "flush"):
            env["DEMO_IO_FAULT"] = fault + ":" + sink
        target = case / "stdout.bin" if sink == "stdout" else Path("/dev/null")
        with target.open("wb") as stdout:
            proc = subprocess.run([str(binary), "--layout" if layout else str(case)], env=env,
                                  stdout=stdout, stderr=subprocess.PIPE, preexec_fn=limited if fault == "limit" else None,
                                  timeout=180)
        stderr = proc.stderr.decode(errors="replace")
        (case / "stderr.log").write_text(stderr)
        ok = proc.returncode == 0 if sink is None else proc.returncode > 0 and "demo_video:" in stderr and "frames (" not in stderr
        if fault in ("flush", "close"):
            ok = ok and "Input/output error" in stderr
        result = {"case": name, "exit": proc.returncode, "passed": ok, "stderr": stderr.strip()}
        results.append(result)
        (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps(result), flush=True)
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
