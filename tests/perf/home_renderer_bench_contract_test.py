#!/usr/bin/env python3
"""Fail closed if the standalone renderer benchmark can compile away its workload."""

from pathlib import Path


source = (Path(__file__).parent / "home_renderer_bench.c").read_text()

assert "assert(" not in source, "benchmark correctness or timing work is hidden in assert"
assert source.count("bench_yield();") >= 3, "each repeated timed stage must yield outside its interval"
assert "vTaskDelay(1)" in source, "ESP benchmark must yield without disabling watchdogs"
assert "CONFIG_SPIRAM" in source, "ESP benchmark must fail closed without configured PSRAM"

print("home renderer benchmark contract: NDEBUG-safe calls, PSRAM guard and stage yields pass")
