#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SOURCE_ROOT="${1:-$ROOT}"
OUT="${2:-${TMPDIR:-/tmp}/home-renderer-bench}"
SOURCE_ROOT="$(cd "$SOURCE_ROOT" && pwd -P)"
FIRMWARE="$SOURCE_ROOT/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
mkdir -p "$(dirname "$OUT")"
"${CC:-cc}" -O2 -Wall -Wextra -Werror -Wmisleading-indentation \
  -I"$FIRMWARE/main" \
  -I"$SOURCE_ROOT/plugins/hermes/firmware" \
  -I"$SOURCE_ROOT/plugins/home_assistant/firmware" \
  -I"$FIRMWARE/components/qrcodegen" \
  "$ROOT/tests/perf/home_renderer_bench.c" \
  "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$OUT"
"$OUT"
