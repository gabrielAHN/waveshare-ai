#!/bin/bash
# Demo video of the board from the firmware's own renderer (host simulation, not a screen recording):
# real gestures through the real UI state (tests/preview/demo_video.c), inside a device-shaped frame.
#
#   tools/demo_video.sh [OUT_DIR]   -> docs/media/waveshare-ai-demo.mp4
#                                      and docs/images/demo.gif (GIF=0 skips the README cut)
# OUT_DIR holds only the compiler, frame templates and provenance receipts (default build/demo).
#
# Needs cc, ImageMagick 7 (`magick`) and ffmpeg with libx264. The frames are streamed straight into
# ffmpeg as raw RGB24 (the PPM payload): ~1,700 frames of 1080x1080 would be ~6 GB on disk
# (DEMO_FRAMES=<dir> keeps them as PPM files anyway).
# OUT_DIR also gets storyboard.txt (what happens when) and gif.txt (the README cut, in MP4 seconds).
# Knobs: FONT=<file> for the captions, DEMO_TOUCH=0 hides the fingertip marker, GIF_FPS / GIF_W.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
OUT="${1:-$ROOT/build/demo}"
MP4="${MP4_OUT:-$ROOT/docs/media/waveshare-ai-demo.mp4}"
mkdir -p "$OUT" "$(dirname "$MP4")"
for tool in magick ffmpeg ffprobe; do command -v "$tool" >/dev/null || { echo "demo_video.sh: needs $tool" >&2; exit 1; }; done

"${CC:-cc}" -O2 -Wall -Wextra -Werror -I"$FIRMWARE/main" -I"$ROOT/plugins/hermes/firmware" -I"$ROOT/plugins/home_assistant/firmware" \
  "$ROOT/tests/preview/demo_video.c" "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$OUT/demo_video"
# Frame geometry and captions: one source of truth, the C file.
eval "$("$OUT/demo_video" --layout)"

# ---- Device frame (ImageMagick): calm background, soft shadow, dark rounded body, side keys ----
BX0=$((SCR_X - BEZEL)); BY0=$((SCR_Y - BEZEL)); BX1=$((SCR_X + SCR_W - 1 + BEZEL)); BY1=$((SCR_Y + SCR_H - 1 + BEZEL))
BR=$((SCR_R + BEZEL))
magick -size "${OUT_W}x${OUT_H}" gradient:'#f2efea-#dbd6ce' \
  \( -size "${OUT_W}x${OUT_H}" xc:none -fill 'rgba(28,22,14,0.45)' \
     -draw "roundrectangle $((BX0 + 6)),$((BY0 + 22)),$((BX1 - 6)),$((BY1 + 18)),$BR,$BR" -blur 0x22 \) -composite \
  -fill '#26272a' -draw "roundrectangle $((BX1 - 8)),232,$((BX1 + 5)),318,5,5" -draw "roundrectangle $((BX1 - 8)),348,$((BX1 + 5)),398,5,5" \
  -fill '#18191b' -stroke '#3c3e42' -strokewidth 2 -draw "roundrectangle $BX0,$BY0,$BX1,$BY1,$BR,$BR" \
  -fill none -stroke 'rgba(255,255,255,0.07)' -strokewidth 1 -draw "roundrectangle $((BX0 + 4)),$((BY0 + 4)),$((BX1 - 4)),$((BY1 - 4)),$((BR - 4)),$((BR - 4))" \
  -stroke none -fill '#050505' -draw "roundrectangle $((SCR_X - 3)),$((SCR_Y - 3)),$((SCR_X + SCR_W + 2)),$((SCR_Y + SCR_H + 2)),$((SCR_R + 3)),$((SCR_R + 3))" \
  "$OUT/device.png"
# The panel's rounded corners: an anti-aliased mask the screen is drawn through.
magick -size "${SCR_W}x${SCR_H}" xc:black -fill white -draw "roundrectangle 0,0,$((SCR_W - 1)),$((SCR_H - 1)),$SCR_R,$SCR_R" \
  -depth 8 "$OUT/mask.pgm"
# One frame per section caption.
if [[ -z "${FONT:-}" ]]; then
  for f in "/System/Library/Fonts/Avenir Next.ttc" "/System/Library/Fonts/HelveticaNeue.ttc" \
           /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf; do
    [[ -f "$f" ]] && { FONT="$f"; break; }
  done
fi
font_args=(); [[ -n "${FONT:-}" ]] && font_args=(-font "$FONT")
for ((k = 0; k < CAPTIONS; k++)); do
  var="CAPTION_$k"
  magick "$OUT/device.png" "${font_args[@]}" -pointsize 38 -fill '#3b3834' -gravity center \
    -annotate "+0+$((CAP_Y - OUT_H / 2))" "${!var}" -depth 8 "$OUT/frame-$k.ppm"
done

# ---- Render + encode: H.264 yuv420p, 30 fps, CRF 18, faststart ----
"$OUT/demo_video" "$OUT" | ffmpeg -y -hide_banner -loglevel error \
  -f rawvideo -pixel_format rgb24 -video_size "${OUT_W}x${OUT_H}" -framerate "$FPS" -i - \
  -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -r "$FPS" -movflags +faststart "$MP4"
# Decode the complete public asset before reporting success.
ffmpeg -v error -i "$MP4" -f null -
echo "$MP4"

# ---- README GIF: the highlight cut (gif.txt), device frame included, one palette for the GIF ----
# Cut from a second, identical render (the run is deterministic) rather than from the MP4: the exact
# pixels keep still areas still, which keeps the GIF small.
if [[ "${GIF:-1}" != 0 ]]; then
  GIF_OUT="${GIF_OUT:-$ROOT/docs/images/demo.gif}"
  GIF_FPS="${GIF_FPS:-12}"; GIF_W="${GIF_W:-360}"; GIF_COLORS="${GIF_COLORS:-96}"
  CROP_X=$((BX0 - 40)); CROP_W=$((BX1 - BX0 + 81))   # the device and its caption, less background
  sel=$(awk '{printf "%sbetween(t,%s,%.3f)", (NR > 1 ? "+" : ""), $1, $2 - 0.001}' "$OUT/gif.txt")
  "$OUT/demo_video" "$OUT" 2>/dev/null | ffmpeg -y -hide_banner -loglevel error \
    -f rawvideo -pixel_format rgb24 -video_size "${OUT_W}x${OUT_H}" -framerate "$FPS" -i - -filter_complex \
    "[0:v]select='$sel',setpts=N/($FPS*TB),fps=$GIF_FPS,crop=$CROP_W:$OUT_H:$CROP_X:0,scale=$GIF_W:-2:flags=lanczos,split[a][b];[a]palettegen=max_colors=$GIF_COLORS:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle" \
    -loop 0 "$GIF_OUT"
  ffmpeg -v error -i "$GIF_OUT" -f null -
  echo "$GIF_OUT"
fi

# Small, public provenance receipt: storyboard/gif cuts come from the renderer, hashes bind the
# final media to the renderer and the core UI it compiled. No raw frame sequence is retained.
{
  printf '%s\n' 'scripted_host_render=1' 'camera_recording=0' 'fixture_data=fictional' 'raw_frame_files=0'
  ffprobe -v error -select_streams v:0 \
    -show_entries stream=codec_name,width,height,r_frame_rate,nb_frames:format=duration \
    -of default=noprint_wrappers=1 "$MP4"
  shasum -a 256 "$ROOT/tests/preview/demo_video.c" "$FIRMWARE/main/home_ui.h" \
    "$FIRMWARE/main/home_render.h" "$OUT/storyboard.txt" "$OUT/gif.txt" "$MP4"
  if [[ "${GIF:-1}" != 0 ]]; then shasum -a 256 "$GIF_OUT"; fi
} >"$OUT/media-provenance.txt"
echo "$OUT/storyboard.txt"
echo "$OUT/media-provenance.txt"
