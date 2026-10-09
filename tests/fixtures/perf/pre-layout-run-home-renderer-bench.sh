#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SOURCE_ROOT="${1:-$ROOT}"
OUT="${2:-${TMPDIR:-/tmp}/home-renderer-bench}"
SOURCE_ROOT="$(cd "$SOURCE_ROOT" && pwd -P)"
mkdir -p "$(dirname "$OUT")"
"${CC:-cc}" -O2 -Wall -Wextra -Werror -Wmisleading-indentation \
  -I"$SOURCE_ROOT/firmware/main" \
  -I"$SOURCE_ROOT/plugins/hermes/firmware" \
  -I"$SOURCE_ROOT/plugins/home_assistant/firmware" \
  -I"$SOURCE_ROOT/firmware/components/qrcodegen" \
  "$ROOT/tests/perf/home_renderer_bench.c" \
  "$SOURCE_ROOT/firmware/components/qrcodegen/qrcodegen.c" -lm -o "$OUT"
"$OUT"
