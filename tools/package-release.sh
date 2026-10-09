#!/bin/bash
# Package the current build as a release folder + zip that tools/flash.sh <folder> can flash
# with only esptool installed (no ESP-IDF).
#   NO_FLASH=1 ./dev.sh && tools/package-release.sh     -> dist/waveshare-ai-<version>/ + .zip
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
BUILD="$FIRMWARE/build"
[[ -f "$BUILD/flash_args" ]] || { echo "No build found: run NO_FLASH=1 ./dev.sh first" >&2; exit 2; }
VERSION="${VERSION:-$(git -C "$ROOT" describe --tags --always --dirty)}"
OUT="$ROOT/dist/waveshare-ai-$VERSION"
rm -rf "$OUT" && mkdir -p "$OUT"
while read -r first rest; do
  [[ "$first" == 0x* ]] || continue
  cp "$BUILD/$rest" "$OUT/$(basename "$rest")"
  printf '%s %s\n' "$first" "$(basename "$rest")"
done < "$BUILD/flash_args" > "$OUT/flash_args.files"
{ head -1 "$BUILD/flash_args" | sed 's/--flash-mode/--flash_mode/; s/--flash-freq/--flash_freq/; s/--flash-size/--flash_size/'; cat "$OUT/flash_args.files"; } > "$OUT/flash_args"
rm "$OUT/flash_args.files"
(cd "$OUT" && shasum -a 256 *.bin > SHA256SUMS)
(cd "$ROOT/dist" && rm -f "waveshare-ai-$VERSION.zip" && zip -qr "waveshare-ai-$VERSION.zip" "waveshare-ai-$VERSION")
echo "$OUT"
echo "$ROOT/dist/waveshare-ai-$VERSION.zip"
