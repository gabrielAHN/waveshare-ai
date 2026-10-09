#!/bin/bash
# Your board's device settings from the git-ignored .env (see .env.example). Sourced by dev.sh.
# Only the five build keys below are read: the Wi-Fi lines in the same file are for tools/set_wifi.py
# alone and never enter the build, its logs or the firmware image.
#   WAVESHARE_AI_PROVIDERS                    hermes, home_assistant (comma list) or none; empty = the Kconfig
#                                          defaults (Hermes only). PROVIDERS=... on the dev.sh command line wins.
#   WAVESHARE_AI_HERMES_TILES                 sparkles, ask (comma list); empty = both
#   WAVESHARE_AI_HERMES_ACCOUNT_NAME          the Hermes tab's sign-in label, 1-16 printable ASCII, no " or \
#   WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME  the Home Assistant tab's label, same rule
#   WAVESHARE_AI_AUTO_SLEEP_MIN               battery: sleep after this many minutes without a touch, 0..120 (0 = never)
LOCAL_CONFIG_KEYS=(WAVESHARE_AI_PROVIDERS WAVESHARE_AI_HERMES_TILES WAVESHARE_AI_HERMES_ACCOUNT_NAME
                   WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME WAVESHARE_AI_AUTO_SLEEP_MIN)

# local_config_value FILE KEY: the last KEY=value in FILE (surrounding quotes removed), or nothing.
# A missing file or key prints nothing and succeeds.
local_config_value() {
  local file="$1" key="$2" line value=""
  [[ -f "$file" ]] || return 0
  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%$'\r'}"
    [[ "$line" == "$key="* ]] || continue
    value="${line#"$key="}"
    if [[ ${#value} -ge 2 && ( "$value" == \"*\" || "$value" == \'*\' ) ]]; then
      value="${value:1:${#value}-2}"
    fi
  done <"$file"
  printf '%s' "$value"
}

# local_config_check_account NAME [KEY]: empty (= the Kconfig default) or 1-16 printable ASCII characters
# without " or \ (it becomes a Kconfig string). Prints why (naming KEY) on stderr and fails otherwise.
local_config_check_account() {
  local name="$1" key="${2:-WAVESHARE_AI_HERMES_ACCOUNT_NAME}"
  [[ -z "$name" ]] && return 0
  if ! LC_ALL=C bash -c '[[ $1 =~ ^[\ -~]{1,16}$ ]]' _ "$name" || [[ "$name" == *\"* || "$name" == *\\* ]]; then
    echo "$key must be 1-16 printable ASCII characters without \" or \\" >&2
    return 1
  fi
}

# local_config_list KEY VALUE ALLOWED...: a comma list (spaces ignored, "-" = "_", any case) of ALLOWED
# names, each at most once; prints them space-separated, or says what is wrong and fails.
local_config_list() {
  local key="$1" value="$2" item out=" " ok a
  shift 2
  value="$(printf '%s' "$value" | tr 'A-Z-' 'a-z_' | tr -d ' \t')"
  local IFS=','
  for item in $value; do
    ok=0
    for a in "$@"; do [[ "$item" == "$a" ]] && ok=1; done
    if [[ $ok == 0 || "$out" == *" $item "* ]]; then
      echo "$key: unknown or repeated '$item' (use: $*)" >&2
      return 1
    fi
    out+="$item "
  done
  out="${out# }"
  out="${out% }"
  if [[ -z "$out" ]]; then
    echo "$key: empty list (use: $*)" >&2
    return 1
  fi
  printf '%s' "$out"
}

# local_config_device_defaults FILE PROVIDERS: the sdkconfig lines for every device setting set in FILE
# (PROVIDERS, when not empty, replaces WAVESHARE_AI_PROVIDERS); nothing when none is set. A setting left
# empty keeps its Kconfig default. Fails (status 2) with a message naming the key otherwise.
local_config_device_defaults() {
  local file="$1" providers="$2" tiles hermes_name home_name sleep p up
  [[ -n "$providers" ]] || providers="$(local_config_value "$file" WAVESHARE_AI_PROVIDERS)"
  tiles="$(local_config_value "$file" WAVESHARE_AI_HERMES_TILES)"
  hermes_name="$(local_config_value "$file" WAVESHARE_AI_HERMES_ACCOUNT_NAME)"
  home_name="$(local_config_value "$file" WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME)"
  sleep="$(local_config_value "$file" WAVESHARE_AI_AUTO_SLEEP_MIN)"
  if [[ -n "$providers" ]]; then
    providers="$(local_config_list WAVESHARE_AI_PROVIDERS "$providers" hermes home_assistant none)" || return 2
    if [[ " $providers " == *" none "* && "$providers" != "none" ]]; then
      echo "WAVESHARE_AI_PROVIDERS: none cannot be combined with a provider" >&2
      return 2
    fi
    for p in hermes home_assistant; do
      up="$(printf '%s' "$p" | tr 'a-z' 'A-Z')"
      if [[ " $providers " == *" $p "* ]]; then echo "CONFIG_WAVESHARE_AI_PROVIDER_$up=y"; else echo "# CONFIG_WAVESHARE_AI_PROVIDER_$up is not set"; fi
    done
  fi
  if [[ -n "$tiles" ]]; then
    tiles="$(local_config_list WAVESHARE_AI_HERMES_TILES "$tiles" sparkles ask)" || return 2
    for p in sparkles ask; do
      up="$(printf '%s' "$p" | tr 'a-z' 'A-Z')"
      if [[ " $tiles " == *" $p "* ]]; then echo "CONFIG_WAVESHARE_AI_HERMES_$up=y"; else echo "# CONFIG_WAVESHARE_AI_HERMES_$up is not set"; fi
    done
  fi
  local_config_check_account "$hermes_name" WAVESHARE_AI_HERMES_ACCOUNT_NAME || return 2
  local_config_check_account "$home_name" WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME || return 2
  [[ -z "$hermes_name" ]] || printf 'CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME="%s"\n' "$hermes_name"
  [[ -z "$home_name" ]] || printf 'CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME="%s"\n' "$home_name"
  if [[ -n "$sleep" ]]; then
    if [[ ! "$sleep" =~ ^[0-9]{1,3}$ ]] || (( 10#$sleep > 120 )); then
      echo "WAVESHARE_AI_AUTO_SLEEP_MIN must be a whole number of minutes, 0..120 (0 = never)" >&2
      return 2
    fi
    echo "CONFIG_WAVESHARE_AI_AUTO_SLEEP_MIN=$((10#$sleep))"
  fi
  return 0
}

# local_config_fresh SDKCONFIG DEFAULTS: a generated sdkconfig only reads its defaults files when it is
# first created, so drop it whenever those files changed (DEFAULTS is the ";"-separated list passed to
# idf.py, relative to the current directory). Remembers their hash next to it.
local_config_fresh() {
  local sdk="$1" defaults="$2" sum old="" f
  local IFS=';'
  sum="$(for f in $defaults; do printf '%s\n' "$f"; cat "$f"; done | shasum -a 256 | cut -c1-64)"
  [[ -f "$sdk.defaults-sha256" ]] && old="$(cat "$sdk.defaults-sha256")"
  if [[ "$sum" != "$old" ]]; then
    rm -f "$sdk"
    mkdir -p "$(dirname "$sdk")"
    printf '%s\n' "$sum" >"$sdk.defaults-sha256"
  fi
}
