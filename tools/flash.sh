#!/bin/bash
# Flash Waveshare AI firmware to the single attached Espressif board (USB VID 0x303a).
#   tools/flash.sh                    flash your own build (NO_FLASH=1 ./dev.sh first; needs ESP-IDF)
#   tools/flash.sh <release-folder>   flash a downloaded release (needs only: pip install esptool)
#   PORT=<serial port> tools/flash.sh choose the port explicitly
#   MONITOR=1 tools/flash.sh          also open idf.py monitor afterwards (own build only)
#   WIFI=0 tools/flash.sh             don't send the .env networks after flashing
# The partition table keeps NVS at the same offset, so Wi-Fi, the device identity and the
# pairing record survive re-flashing. Never erase-flash unless you intend a factory reset.
# After a successful flash, the networks in the repo's .env (home Wi-Fi and/or the iPhone hotspot,
# see .env.example) are sent to the board over USB by tools/set_wifi.py --env. Nothing is printed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
: "${HOME:?HOME must be set}"
# Send the .env networks once the flash worked (set_wifi.py refuses a world-readable .env).
send_wifi() {
  local py="$1"
  [[ "${WIFI:-1}" == 1 && -f "$ROOT/.env" ]] || return 0
  grep -qE '^WAVESHARE_AI_(WIFI|HOTSPOT)_SSID=.+' "$ROOT/.env" || return 0
  echo "Sending the .env Wi-Fi networks to the board"
  PORT="$PORT" "$py" "$ROOT/tools/set_wifi.py" --env "$ROOT/.env" --port "$PORT" || echo "Wi-Fi not sent (flash is fine): run tools/set_wifi.py --env" >&2
}

if [[ $# -ge 1 ]]; then
  DIR="$(cd "$1" && pwd)"
  [[ -f "$DIR/flash_args" ]] || { echo "$DIR has no flash_args: not a release folder" >&2; exit 2; }
  if python3 -c 'import esptool' 2>/dev/null; then PY=python3
  elif command -v esptool >/dev/null 2>&1; then PY="$(head -1 "$(command -v esptool)" | sed -n 's/^#!//p')"
  else echo "esptool not found: run 'python3 -m pip install esptool'" >&2; exit 2; fi
  PORT="${PORT:-$("$PY" "$ROOT/tools/find_port.py")}"
  echo "Flashing $DIR -> $PORT"
  cd "$DIR"
  "$PY" -m esptool --chip esp32s3 -p "$PORT" -b 460800 --before default_reset --after hard_reset write_flash @flash_args
  send_wifi "$PY"
  exit 0
fi

export IDF_PATH="${IDF_PATH:-$ROOT/../esp-idf-6.0.2}"
[[ -f "$IDF_PATH/export.sh" ]] || { echo "Set IDF_PATH to an ESP-IDF 6.0.x checkout" >&2; exit 2; }
BUILD="$FIRMWARE/build"
[[ -f "$BUILD/flash_args" ]] || { echo "No build found: run NO_FLASH=1 ./dev.sh first" >&2; exit 2; }
if [[ -z "${IDF_PYTHON_ENV_PATH:-}" ]]; then
  for env in "$HOME"/.espressif/python_env/idf6.0_py3.*_env; do
    [[ -x "$env/bin/python" ]] && export IDF_PYTHON_ENV_PATH="$env" && break
  done
fi
set +u
source "$IDF_PATH/export.sh" >/dev/null
set -u
PORT="${PORT:-$(python "$ROOT/tools/find_port.py")}"
echo "Flashing $BUILD -> $PORT"
cd "$BUILD"  # flash_args uses build-relative paths
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 --before default-reset --after hard-reset write-flash @flash_args
send_wifi python
if [[ "${MONITOR:-0}" == 1 ]]; then
  cd "$FIRMWARE" && idf.py -p "$PORT" monitor
fi
