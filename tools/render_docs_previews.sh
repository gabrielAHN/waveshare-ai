#!/bin/bash
# Render the README pictures from the real renderer (host simulation, not panel photos):
#   tools/render_docs_previews.sh   -> docs/images/*.png and plugins/<plugin>/images/*.png
#                                      (needs cc + ImageMagick 7 `magick`)
# Frames named <plugin>--<name> go directly to that plugin's own image folder.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
OUT="${DOCS_PREVIEW_OUT:-$ROOT/build/docs-preview}"
mkdir -p "$OUT" "$ROOT/docs/images"
"${CC:-cc}" -O2 -Wall -Wextra -Werror -I"$FIRMWARE/main" -I"$ROOT/plugins/hermes/firmware" -I"$ROOT/plugins/home_assistant/firmware" -I"$FIRMWARE/components/qrcodegen" \
  "$ROOT/tests/preview/docs_preview.c" "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$OUT/docs_preview"
rm -f "$OUT"/*.ppm
"$OUT/docs_preview" "$OUT" >/dev/null
for f in "$OUT"/*.ppm; do
  name="$(basename "$f" .ppm)"
  if [[ "$name" == *--* ]]; then
    dest="plugins/${name%%--*}/images/${name#*--}.png"
  else
    dest="docs/images/$name.png"
  fi
  mkdir -p "$ROOT/$(dirname "$dest")"
  # half size keeps the repo small; strip metadata so re-renders are byte-stable
  magick "$f" -resize 50% -strip "$ROOT/$dest"
  echo "$dest"
done
