#!/usr/bin/env python3
"""Bounded motion-preview output regressions; --out must be a fresh scratch directory.

Real RLIMIT_FSIZE failures and attested stdio faults use /dev/null PPM sinks.
Only the positive control retains pixels: 45 complete PPMs (about 22 MB).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
STRIPS = {"ask-drop": 12, "sparkles-drop": 12, "back": 9, "carousel": 6, "open": 6}
FRAMES = sorted(f"motion-{strip}-{n}.ppm" for strip, count in STRIPS.items() for n in range(count))
HEADER = b"P6\n368 448\n255\n"
CASES = [("labels", "limit", "labels"), ("labels", "fprintf", "labels"),
         ("labels", "fflush", "labels"), ("labels", "fclose", "labels"),
         ("frame", "limit", "frame"), ("frame", "fprintf", "frame"),
         ("frame", "fwrite", "frame"), ("frame", "fflush", "frame"),
         ("frame", "fclose", "frame"), ("stdout", "limit", "stdout"),
         ("stdout", "printf", "stdout"), ("stdout", "fflush", "stdout"),
         ("stdout", "fclose", "stdout")]


def limited():
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--group", choices=("all", "labels", "frame", "stdout", "success"), default="all")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    command = [os.environ.get("CC", "cc"), "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"]
    for inc in (FIRMWARE / "main", ROOT / "plugins/hermes/firmware", ROOT / "plugins/home_assistant/firmware", FIRMWARE / "components/qrcodegen"):
        command += ["-I" + str(inc)]
    binaries = {}
    for key, filename in (("plain", "motion_preview.c"), ("fault", "motion_io_faults.c")):
        binaries[key] = out / key
        cmd = command + [str(ROOT / "tests/preview" / filename),
                         str(FIRMWARE / "components/qrcodegen/qrcodegen.c"), "-lm", "-o", str(binaries[key])]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
        (out / (key + "-compile.log")).write_text(proc.stdout + proc.stderr)
        if proc.returncode:
            raise RuntimeError(f"{key} compile failed; see {out}")
    cases: list[tuple[str, str | None, str | None]] = [
        (op + "-" + sink, op, sink) for group, op, sink in CASES if args.group in ("all", group)]
    if args.group in ("all", "success"):
        cases.append(("success", None, None))
    if not cases:
        raise RuntimeError("No cases selected")
    results = []
    for name, op, sink in cases:
        case = out / name
        case.mkdir()
        if sink:
            for filename in FRAMES:
                (case / filename).symlink_to("/dev/null")
            if not (op == "limit" and sink == "labels"):
                (case / "motion-labels.txt").symlink_to("/dev/null")
        if op == "limit" and sink == "frame":
            # The first rendered frame must hit the file-size limit.
            (case / "motion-ask-drop-0.ppm").unlink()
        env = os.environ.copy()
        env.pop("MOTION_IO_FAULT", None)
        if op and op != "limit" and sink:
            env["MOTION_IO_FAULT"] = op + ":" + sink
        binary = binaries["fault" if op and op != "limit" else "plain"]
        stdout_path = case / "stdout.log"
        with stdout_path.open("wb") as stdout:
            # RLIMIT_FSIZE must affect only the selected sink, not captured progress.
            target = stdout if op == "limit" and sink == "stdout" else subprocess.PIPE
            proc = subprocess.run([str(binary), str(case)], env=env, stdout=target, stderr=subprocess.PIPE,
                                  preexec_fn=limited if op == "limit" else None, timeout=90)
            if proc.stdout is not None:
                stdout.write(proc.stdout)
        stderr = proc.stderr.decode(errors="replace")
        (case / "stderr.log").write_text(stderr)
        details = {}
        if sink:
            error_sink = "frame" if sink == "frame" else sink
            ok = proc.returncode > 0 and "motion_preview: output " + error_sink + ":" in stderr
            attestations = re.findall(r"MOTION_IO_FAULT op=(\w+) sink=(\w+) real=(-?\d+) injected=-1", stderr)
            if op != "limit":
                expected_real = 0 if op in ("fflush", "fclose") else None
                ok = ok and len(attestations) == 1 and attestations[0][:2] == (op, sink)
                if attestations:
                    real = int(attestations[0][2])
                    ok = ok and (real == expected_real if expected_real is not None else real > 0)
                ok = ok and "Input/output error" in stderr
            details["attestations"] = attestations
            if op == "limit":
                details["limited_bytes"] = (stdout_path if sink == "stdout" else case / (
                    "motion-labels.txt" if sink == "labels" else "motion-ask-drop-0.ppm")).stat().st_size
                ok = ok and details["limited_bytes"] == 0
            details["reported_frames"] = len(stdout_path.read_text().splitlines())
        else:
            labels = (case / "motion-labels.txt").read_text().splitlines()
            frames = sorted(p.name for p in case.glob("motion-*.ppm"))
            hashes = {}
            ok = proc.returncode == 0 and not stderr and frames == FRAMES and len(labels) == 45
            expected_names = sorted(f"motion-{line.split()[0]}-{line.split()[1]}.ppm" for line in labels)
            ok = ok and expected_names == FRAMES and len(stdout_path.read_text().splitlines()) == 45
            pixels = hashlib.sha256()
            for filename in frames:
                data = (case / filename).read_bytes()
                ok = ok and data.startswith(HEADER) and len(data) == len(HEADER) + 368 * 448 * 3
                hashes[filename] = hashlib.sha256(data).hexdigest()
                pixels.update(data[len(HEADER):])
            details.update(frames=len(frames), labels=len(labels), frame_sha256=hashes,
                           pixel_sequence_sha256=pixels.hexdigest(),
                           labels_sha256=hashlib.sha256((case / "motion-labels.txt").read_bytes()).hexdigest())
        ok = ok and "AddressSanitizer" not in stderr and "runtime error:" not in stderr
        result = {"case": name, "exit": proc.returncode, "passed": ok, "stderr": stderr.strip(), **details}
        results.append(result)
        (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps({key: value for key, value in result.items() if key != "frame_sha256"}), flush=True)
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
