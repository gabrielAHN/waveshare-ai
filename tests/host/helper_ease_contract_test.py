#!/usr/bin/env python3
"""The chat arrival scalar uses the guarded shared CubicOut wrapper."""

from pathlib import Path
import re


source = (Path(__file__).parents[2] / "devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main/home_render.h").read_text()
match = re.search(r"static inline float helper_ease\(float t\)\{([^}]+)\}", source)

assert match is not None
assert match.group(1).strip() == "return ui_ease_cubic_out(t,0.f,1.f,1.f);"

print("helper easing contract: shared guarded CubicOut caller")
