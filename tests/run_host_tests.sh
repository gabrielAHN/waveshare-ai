#!/bin/bash
# Compile and run every host C suite (tests/host/*_test.c) under ASan/UBSan.
# Usage: tests/run_host_tests.sh [suite_name ...]   (default: all suites)
# SANITIZE=0 disables sanitizers (e.g. for a quick -O2 timing run).
# HOST_TEST_DEFINES="-DWAVESHARE_AI_PROVIDER_HOME_ASSISTANT=0 ..." compiles the named suites for a provider /
# tile combination (the named device's firmware/main/plugins.h; e.g. `... tests/run_host_tests.sh plugin_tiles_test`). The
# feature suites assume the default host build (both providers, every tile on). With no suite arguments
# and no HOST_TEST_DEFINES, the default run is followed by the plugin matrix (the MATRIX_SUITES below
# built for every tile combination in PLUGIN_MATRIX, with the providers those tiles imply), the
# provider builds (PROVIDER_SUITES with one provider off) and the sign-in label builds.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="$ROOT/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
OUT="${HOST_TEST_OUT:-$ROOT/build/host-tests}"
mkdir -p "$OUT"
CC="${CC:-cc}"
FLAGS=(-O1 -g -Wall -Wextra -Werror -Wmisleading-indentation)
# The device core and each plugin's firmware folder (suites include plain names: #include "home_ui.h").
FLAGS+=(-I"$FIRMWARE/main" -I"$ROOT/plugins/hermes/firmware" -I"$ROOT/plugins/home_assistant/firmware" -I"$FIRMWARE/components/qrcodegen")
read -r -a DEFINES <<< "${HOST_TEST_DEFINES:-}"
FLAGS+=("${DEFINES[@]+"${DEFINES[@]}"}")
if [[ "${SANITIZE:-1}" == 1 ]]; then
  FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
fi
if [[ $# -gt 0 ]]; then
  suites=("$@")
else
  suites=()
  for f in "$ROOT"/tests/host/*_test.c; do suites+=("$(basename "$f" .c)"); done
fi
# Keep the panel, completion sound, carousel, Home pull/contact and blob regression suites in the default run.
for required in panel_power_test completion_click_test home_motion_test home_pull_test home_bottom_consistency_test home_bottom_contact_test home_blob_test home_accent_exit_test home_center_exit_test; do
  [[ -f "$ROOT/tests/host/$required.c" ]] || { echo "missing required suite: $required"; exit 1; }
done
pass=0
for s in "${suites[@]}"; do
  # Vendored nayuki qrcodegen (MIT) links into every suite: home_render.h draws the phone sign-in QR.
  "$CC" "${FLAGS[@]}" "$ROOT/tests/host/$s.c" "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$OUT/$s"
  (cd "$ROOT" && "$OUT/$s") >"$OUT/$s.log" 2>&1 || { echo "FAIL $s"; cat "$OUT/$s.log"; exit 1; }
  pass=$((pass + 1))
done
echo "HOST_C_SUITES pass=$pass total=${#suites[@]}${HOST_TEST_DEFINES:+ defines=$HOST_TEST_DEFINES}"
# Plugin matrix: SPARKLES AI HOME_ASSISTANT as 0/1 (111 = the default build above). The providers follow
# the tiles exactly as main/CMakeLists.txt maps them: Hermes = Sparkles or Ask, Home Assistant = Sensor.
if [[ $# -eq 0 && -z "${HOST_TEST_DEFINES:-}" ]]; then
  PLUGIN_MATRIX=(000 001 010 011 100 101 110 111)
  MATRIX_SUITES=(plugin_tiles_test tile_plugins_test wifi_flash_only_test)
  mpass=0
  for combo in "${PLUGIN_MATRIX[@]}"; do
    hermes=0; [[ "${combo:0:2}" != 00 ]] && hermes=1
    defs=(-DWAVESHARE_AI_PLUGIN_SPARKLES="${combo:0:1}" -DWAVESHARE_AI_PLUGIN_AI="${combo:1:1}" -DWAVESHARE_AI_PLUGIN_HOME_ASSISTANT="${combo:2:1}"
          -DWAVESHARE_AI_PROVIDER_HERMES="$hermes" -DWAVESHARE_AI_PROVIDER_HOME_ASSISTANT="${combo:2:1}")
    for suite in "${MATRIX_SUITES[@]}"; do
      bin="$OUT/${suite}_$combo"
      "$CC" "${FLAGS[@]}" "${defs[@]}" "$ROOT/tests/host/$suite.c" "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$bin"
      (cd "$ROOT" && "$bin") >"$bin.log" 2>&1 || { echo "FAIL $suite ${defs[*]}"; cat "$bin.log"; exit 1; }
    done
    mpass=$((mpass + 1))
  done
  echo "PLUGIN_MATRIX pass=$mpass total=${#PLUGIN_MATRIX[@]} suites=${MATRIX_SUITES[*]} (sparkles,ai,home_assistant: ${PLUGIN_MATRIX[*]})"
  # Provider builds (Kconfig "Waveshare AI providers"): one provider off at a time; the default run above has both.
  PROVIDER_BUILDS=(hermes-only home-assistant-only)
  PROVIDER_SUITES=(provider_signin_test)
  ppass=0
  for build in "${PROVIDER_BUILDS[@]}"; do
    if [[ "$build" == hermes-only ]]; then defs=(-DWAVESHARE_AI_PROVIDER_HOME_ASSISTANT=0); else defs=(-DWAVESHARE_AI_PROVIDER_HERMES=0); fi
    for suite in "${PROVIDER_SUITES[@]}"; do
      bin="$OUT/${suite}_$build"
      "$CC" "${FLAGS[@]}" "${defs[@]}" "$ROOT/tests/host/$suite.c" "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$bin"
      (cd "$ROOT" && "$bin") >"$bin.log" 2>&1 || { echo "FAIL $suite ${defs[*]}"; cat "$bin.log"; exit 1; }
    done
    ppass=$((ppass + 1))
  done
  echo "PROVIDER_BUILDS pass=$ppass total=${#PROVIDER_BUILDS[@]} suites=${PROVIDER_SUITES[*]} (${PROVIDER_BUILDS[*]}; both providers above)"
  # Sign-in labels (the named device's firmware/main/account.h, Kconfig CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME and
  # CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME): the longest name that still fits "<name>: signed in as"
  # and the longest allowed name pass every Settings check on both provider tabs.
  ACCOUNT_NAMES=("Authentik" "Example Identity")
  apass=0
  for name in "${ACCOUNT_NAMES[@]}"; do
    bin="$OUT/settings_ui_test_account$apass"
    "$CC" "${FLAGS[@]}" "-DWAVESHARE_AI_HERMES_ACCOUNT_NAME=\"$name\"" "-DWAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME=\"$name\"" "$ROOT/tests/host/settings_ui_test.c" "$FIRMWARE/components/qrcodegen/qrcodegen.c" -lm -o "$bin"
    (cd "$ROOT" && "$bin") >"$bin.log" 2>&1 || { echo "FAIL settings_ui_test WAVESHARE_AI_HERMES_ACCOUNT_NAME=WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME=$name"; cat "$bin.log"; exit 1; }
    apass=$((apass + 1))
  done
  echo "ACCOUNT_NAMES pass=$apass total=${#ACCOUNT_NAMES[@]} suite=settings_ui_test (${ACCOUNT_NAMES[*]} as both labels; defaults Hermes / Home Assistant above)"
  # Source-level acceptance of the single current firmware contract.
  python3 "$ROOT/tests/host/current_contract_test.py"
  python3 "$ROOT/tests/host/firmware_identity_test.py"
  python3 "$ROOT/tests/host/device_layout_test.py"
  python3 "$ROOT/tests/host/device_path_contract_test.py"
  python3 "$ROOT/tests/host/release_docs_commands_test.py"
  # Your board's .env settings (tiles, label) as dev.sh reads them.
  /bin/bash "$ROOT/tests/host/local_config_test.sh"
fi
