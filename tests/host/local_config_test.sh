#!/bin/bash
# tools/local_config.sh + dev.sh device settings: reading .env without touching its Wi-Fi lines, the five
# device keys (providers, Hermes tiles, two account labels, auto-sleep), the
# PROVIDERS= override, and the one generated named-device build/device.defaults + sdkconfig.device that is
# rebuilt when its defaults change. dev.sh's own lines run against a scratch tree.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/tools/local_config.sh"
T="$(mktemp -d "${TMPDIR:-/tmp}/local-config-test.XXXXXX")"
trap 'python3 -c "import shutil,sys; shutil.rmtree(sys.argv[1])" "$T"' EXIT
pass=0
ok() { pass=$((pass + 1)); }
fail() { echo "FAIL local_config_test: $*" >&2; exit 1; }

# --- reading .env -------------------------------------------------------------------------------------
cat >"$T/.env" <<'EOF'
# a comment WAVESHARE_AI_PROVIDERS=wrong
WAVESHARE_AI_WIFI_SSID=Sam's iPhone
WAVESHARE_AI_WIFI_PASSWORD=pw-NEVER-READ-123
WAVESHARE_AI_PROVIDERS=hermes
WAVESHARE_AI_PROVIDERS="hermes,home_assistant"
WAVESHARE_AI_HERMES_ACCOUNT_NAME='My Lab'
WAVESHARE_AI_PROVIDERS_EXTRA=nope
EOF
[[ "$(local_config_value "$T/.env" WAVESHARE_AI_PROVIDERS)" == "hermes,home_assistant" ]] || fail "last value wins, quotes removed"; ok
[[ "$(local_config_value "$T/.env" WAVESHARE_AI_HERMES_ACCOUNT_NAME)" == "My Lab" ]] || fail "single quotes removed"; ok
[[ -z "$(local_config_value "$T/.env" WAVESHARE_AI_MISSING)" ]] || fail "missing key is empty"; ok
[[ -z "$(local_config_value "$T/nope.env" WAVESHARE_AI_PROVIDERS)" ]] || fail "missing file is empty"; ok
out="$(local_config_device_defaults "$T/.env" "" 2>&1 || true)"
[[ "$out" != *NEVER-READ* && "$out" != *iPhone* ]] || fail "Wi-Fi values leaked"; ok
printf 'WAVESHARE_AI_HERMES_TILES=ask\r\n' >"$T/crlf.env"
[[ "$(local_config_value "$T/crlf.env" WAVESHARE_AI_HERMES_TILES)" == "ask" ]] || fail "CRLF line"; ok
printf 'WAVESHARE_AI_HERMES_TILES=sparkles' >"$T/nolf.env"
[[ "$(local_config_value "$T/nolf.env" WAVESHARE_AI_HERMES_TILES)" == "sparkles" ]] || fail "last line without newline"; ok
# --- account labels -----------------------------------------------------------------------------------
for good in "" "Hermes" "Home Assistant" "Example Identity" "a-b_c.d (1)"; do
  local_config_check_account "$good" 2>/dev/null || fail "rejected account '$good'"; ok
done
for bad in "Seventeen chars!!" 'a"b' 'a\b' "Café" $'tab\there'; do
  if local_config_check_account "$bad" 2>/dev/null; then fail "accepted account '$bad'"; fi; ok
done
msg="$(local_config_check_account "Seventeen chars!!" WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME 2>&1 || true)"
[[ "$msg" == WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME* ]] || fail "label error names its key ($msg)"; ok

# --- device defaults ----------------------------------------------------------------------------------
defaults() {  # defaults "KEY=value..." [PROVIDERS] -> the lines, or "exit N: message"
  printf '%s\n' "$1" >"$T/d.env"
  local got rc=0
  got="$(local_config_device_defaults "$T/d.env" "${2:-}" 2>"$T/d.err")" || rc=$?
  if [[ $rc != 0 ]]; then printf 'exit %s: %s' "$rc" "$(cat "$T/d.err")"; else printf '%s' "$got"; fi
}
nl=$'\n'
[[ "$(defaults 'WAVESHARE_AI_WIFI_SSID=x')" == "" ]] || fail "no device key: no lines"; ok
[[ "$(defaults $'WAVESHARE_AI_PROVIDERS=\nWAVESHARE_AI_HERMES_TILES=')" == "" ]] || fail "empty values keep the defaults"; ok
[[ "$(defaults 'WAVESHARE_AI_PROVIDERS=hermes')" == "CONFIG_WAVESHARE_AI_PROVIDER_HERMES=y$nl# CONFIG_WAVESHARE_AI_PROVIDER_HOME_ASSISTANT is not set" ]] \
  || fail "providers: hermes only"; ok
[[ "$(defaults 'WAVESHARE_AI_PROVIDERS= Home-Assistant , hermes')" == "CONFIG_WAVESHARE_AI_PROVIDER_HERMES=y${nl}CONFIG_WAVESHARE_AI_PROVIDER_HOME_ASSISTANT=y" ]] \
  || fail "providers: both (spaces, case, dash)"; ok
[[ "$(defaults 'WAVESHARE_AI_PROVIDERS=none')" == "# CONFIG_WAVESHARE_AI_PROVIDER_HERMES is not set$nl# CONFIG_WAVESHARE_AI_PROVIDER_HOME_ASSISTANT is not set" ]] \
  || fail "providers: none = the device core only"; ok
[[ "$(defaults 'WAVESHARE_AI_PROVIDERS=hermes' 'home_assistant')" == "# CONFIG_WAVESHARE_AI_PROVIDER_HERMES is not set${nl}CONFIG_WAVESHARE_AI_PROVIDER_HOME_ASSISTANT=y" ]] \
  || fail "PROVIDERS= overrides WAVESHARE_AI_PROVIDERS"; ok
for bad in 'WAVESHARE_AI_PROVIDERS=ai' 'WAVESHARE_AI_PROVIDERS=none,hermes' 'WAVESHARE_AI_PROVIDERS=hermes,hermes' 'WAVESHARE_AI_PROVIDERS=,' \
           'WAVESHARE_AI_PROVIDERS=" "'; do
  [[ "$(defaults "$bad")" == "exit 2: WAVESHARE_AI_PROVIDERS"* ]] || fail "providers: refused '$bad' naming the key"; ok
done
[[ "$(defaults 'WAVESHARE_AI_HERMES_TILES=ask')" == "# CONFIG_WAVESHARE_AI_HERMES_SPARKLES is not set${nl}CONFIG_WAVESHARE_AI_HERMES_ASK=y" ]] \
  || fail "Hermes tiles: ask only"; ok
[[ "$(defaults 'WAVESHARE_AI_HERMES_TILES=ai')" == "exit 2: WAVESHARE_AI_HERMES_TILES"* ]] || fail "Hermes tiles: 'ai' is not a tile"; ok
[[ "$(defaults 'WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME="Sensors"')" == 'CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME="Sensors"' ]] \
  || fail "Home Assistant label"; ok
[[ "$(defaults 'WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME=Way too long a label')" == "exit 2: WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME"* ]] \
  || fail "Home Assistant label too long"; ok
[[ "$(defaults 'WAVESHARE_AI_AUTO_SLEEP_MIN=007')" == "CONFIG_WAVESHARE_AI_AUTO_SLEEP_MIN=7" ]] || fail "auto-sleep minutes"; ok
[[ "$(defaults 'WAVESHARE_AI_AUTO_SLEEP_MIN=120')" == "CONFIG_WAVESHARE_AI_AUTO_SLEEP_MIN=120" ]] || fail "auto-sleep 120 is the top"; ok
for bad in 121 -1 5m 1.5 1000; do
  [[ "$(defaults "WAVESHARE_AI_AUTO_SLEEP_MIN=$bad")" == "exit 2: WAVESHARE_AI_AUTO_SLEEP_MIN"* ]] || fail "auto-sleep refused '$bad'"; ok
done
for key in "${LOCAL_CONFIG_KEYS[@]}"; do
  grep -q "^$key=" "$ROOT/.env.example" || fail ".env.example lists $key"; ok
done
[[ "$(grep -oE 'local_config_value "\$file" [A-Z_]+' "$ROOT/tools/local_config.sh" | awk '{print $3}' | sort -u | tr '\n' ' ')" \
   == "$(printf '%s\n' "${LOCAL_CONFIG_KEYS[@]}" | sort | tr '\n' ' ')" ]] || fail "the build reads exactly the five device keys"; ok

# --- sdkconfig refresh --------------------------------------------------------------------------------
mkdir -p "$T/fw/build"
cd "$T/fw"
printf 'CONFIG_A=y\n' >sdkconfig.defaults
printf 'CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME="One"\n' >build/device.defaults
D="sdkconfig.defaults;build/device.defaults"
local_config_fresh "$T/fw/build/sdkconfig.x" "$D"
[[ -s build/sdkconfig.x.defaults-sha256 ]] || fail "hash written"; ok
echo generated >build/sdkconfig.x
local_config_fresh "$T/fw/build/sdkconfig.x" "$D"
[[ -f build/sdkconfig.x ]] || fail "unchanged defaults keep the sdkconfig"; ok
printf 'CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME="Two"\n' >build/device.defaults
local_config_fresh "$T/fw/build/sdkconfig.x" "$D"
[[ ! -f build/sdkconfig.x ]] || fail "changed defaults drop the sdkconfig"; ok
echo generated >build/sdkconfig.x
local_config_fresh "$T/fw/build/sdkconfig.x" "sdkconfig.defaults"
[[ ! -f build/sdkconfig.x ]] || fail "a different defaults list drops the sdkconfig"; ok

# --- dev.sh itself: its device lines and its sdkconfig block (up to idf.py) against a scratch tree -----
devsh_preset() {  # devsh_preset ROOT -> prints the -DSDKCONFIG and -DSDKCONFIG_DEFAULTS it would pass
  (
    ROOT="$1"; cd "$ROOT"
    eval "$(sed -n '/^# Device settings/,/^DEVICE_DEFAULTS=/p' "$REPO/dev.sh")"
    cd "$FIRMWARE"
    eval "$(sed -n '/^# Always explicit/,/^idf.py /p' "$REPO/dev.sh" | sed '$d')"
    printf '%s\n' "${PRESET[@]}"
  )
}
REPO="$ROOT"
R="$T/repo"; F="$R/devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware"
mkdir -p "$F"; printf 'CONFIG_A=y\n' >"$F/sdkconfig.defaults"
out="$(unset PROVIDERS; DEVICE_DEFAULTS=stray devsh_preset "$R")"
[[ "$out" == "-DSDKCONFIG=$F/sdkconfig"$'\n'"-DSDKCONFIG_DEFAULTS=sdkconfig.defaults" ]] || fail "no .env: default build ($out)"; ok
[[ ! -e "$F/build/device.defaults" ]] || fail "no .env: nothing generated"; ok
cat >"$R/.env" <<'EOF'
WAVESHARE_AI_WIFI_PASSWORD=pw-NEVER-READ-123
WAVESHARE_AI_PROVIDERS=hermes,home_assistant
WAVESHARE_AI_HERMES_TILES=sparkles,ask
WAVESHARE_AI_HERMES_ACCOUNT_NAME="My Lab"
WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME=Sensors
WAVESHARE_AI_AUTO_SLEEP_MIN=10
EOF
out="$(unset PROVIDERS; devsh_preset "$R")"
[[ "$out" == "-DSDKCONFIG=$F/build/sdkconfig.device"$'\n'"-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;build/device.defaults" ]] \
  || fail ".env device settings: sdkconfig.device ($out)"; ok
[[ "$(grep -v '^#' "$F/build/device.defaults")" == 'CONFIG_WAVESHARE_AI_PROVIDER_HERMES=y
CONFIG_WAVESHARE_AI_PROVIDER_HOME_ASSISTANT=y
CONFIG_WAVESHARE_AI_HERMES_SPARKLES=y
CONFIG_WAVESHARE_AI_HERMES_ASK=y
CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME="My Lab"
CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME="Sensors"
CONFIG_WAVESHARE_AI_AUTO_SLEEP_MIN=10' ]] || fail "device.defaults holds every setting"; ok
! grep -rq NEVER-READ "$F" || fail "Wi-Fi value in the build tree"; ok
echo generated >"$F/build/sdkconfig.device"
(unset PROVIDERS; devsh_preset "$R") >/dev/null
[[ -f "$F/build/sdkconfig.device" ]] || fail "same .env keeps the generated sdkconfig"; ok
sed -i.bak 's/^WAVESHARE_AI_AUTO_SLEEP_MIN=10$/WAVESHARE_AI_AUTO_SLEEP_MIN=15/' "$R/.env"
(unset PROVIDERS; devsh_preset "$R") >/dev/null
[[ ! -f "$F/build/sdkconfig.device" ]] || fail "a changed setting regenerates the sdkconfig"; ok
(PROVIDERS=none devsh_preset "$R") >/dev/null
grep -qx '# CONFIG_WAVESHARE_AI_PROVIDER_HERMES is not set' "$F/build/device.defaults" || fail "dev.sh: PROVIDERS= overrides .env"; ok
printf 'WAVESHARE_AI_HERMES_ACCOUNT_NAME=Way too long a label\n' >"$R/.env"
if (unset PROVIDERS; devsh_preset "$R") >/dev/null 2>&1; then fail "dev.sh accepted a bad label"; fi; ok

echo "LOCAL_CONFIG pass=$pass total=$pass (.env device keys, Wi-Fi lines untouched, device.defaults refresh, dev.sh PROVIDERS)"
