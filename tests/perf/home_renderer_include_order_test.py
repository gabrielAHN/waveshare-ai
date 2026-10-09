#!/usr/bin/env python3
"""Guard the ESP timer declaration before product headers expand SP_STAMP."""

from pathlib import Path


source = (Path(__file__).parent / "home_renderer_bench.c").read_text()
timer = source.index('#include "esp_timer.h"')
renderer = source.index('#include "home_render.h"')

assert source.rfind("#ifdef ESP_PLATFORM", 0, timer) >= 0
assert timer < renderer, "esp_timer.h must precede home_render.h for ESP_PLATFORM"

print("home renderer include order: ESP timer declaration precedes product renderer")
