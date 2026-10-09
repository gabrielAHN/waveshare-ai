#!/bin/bash
# Render the Ask bot art off-device: every look x mood (also mirrored and chat-sized), a frame strip of
# every animation (idle routines, nap, wake, celebrate, tap, lean-in, nod), each look's Ask page and the
# Home Ask tile.
#   tools/bot_preview.sh [OUT_DIR]        (default build/bot-preview; the folder is recreated)
# Needs only a host C compiler; with ImageMagick (`magick`) it also writes PNGs and sheets:
#   sheet.png (one row per look, columns = moods), mirrored.png, compact.png, animations.png (one row per
#   animation, helper tee then coding hoodie) and zoom/<animation>.png (each strip x3, nearest-neighbour).
# See plugins/hermes/KOTARO.md.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
cd "$ROOT"
OUT="${1:-build/bot-preview}"
rm -rf "$OUT"
mkdir -p "$OUT"
cc -O1 -Wall -Wextra -Werror -I"$FIRMWARE/main" -Iplugins/hermes/firmware -Iplugins/home_assistant/firmware tools/bot_preview.c "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$OUT/bot_preview"
"$OUT/bot_preview" "$OUT"
if command -v magick >/dev/null 2>&1; then
  for f in "$OUT"/*.ppm; do magick "$f" "${f%.ppm}.png" && rm "$f"; done
  # sheet NAME PREFIX: one row per look (columns = moods)
  sheet() {
    local rows=() row look
    for row in "$OUT/$2"look*-00-idle.png; do
      look="$(basename "$row" | sed -e "s/^$2//" | cut -d- -f1)"
      magick "$OUT/$2$look"-*.png -bordercolor '#333' -border 3 +append "$OUT/row-$2$look.png"
      rows+=("$OUT/row-$2$look.png")
    done
    magick "${rows[@]}" -append "$OUT/$1"
  }
  sheet sheet.png ""
  sheet mirrored.png mirror-
  sheet compact.png compact-
  mkdir -p "$OUT/zoom"
  strips=()
  for first in "$OUT"/anim*-0.png; do
    name="$(basename "$first" -0.png)"
    frames=()
    for f in 0 1 2 3 4 5 6 7 8 9; do frames+=("$OUT/$name-$f.png"); done
    magick "${frames[@]}" -bordercolor '#333' -border 3 +append "$OUT/strip-$name.png"
    strips+=("$OUT/strip-$name.png")
    magick \( "${frames[@]:0:5}" -bordercolor '#333' -border 3 +append \) \( "${frames[@]:5:5}" -bordercolor '#333' -border 3 +append \) \
      -append -filter point -resize 300% "$OUT/zoom/$name.png"
  done
  magick "${strips[@]}" -append "$OUT/animations.png"
  echo "sheets: $OUT/sheet.png $OUT/mirrored.png $OUT/compact.png $OUT/animations.png (zoomed strips in $OUT/zoom/)"
else
  echo "PPM files in $OUT (install ImageMagick for PNGs, or open them with Preview)"
fi
