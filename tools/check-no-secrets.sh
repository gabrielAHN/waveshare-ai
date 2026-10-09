#!/bin/bash
# Fail if committed content contains credentials or site-specific values.
#   tools/check-no-secrets.sh          scan the files tracked (or staged) in the working tree
#   tools/check-no-secrets.sh --history  also scan every blob in every commit
# Patterns: PEM blocks, 64-hex tokens, RFC1918 IPv4 addresses, USB serial device paths, macOS
# home paths, JWTs, and any extra site words listed one per line in $WAVESHARE_AI_DENYLIST (a file
# kept OUTSIDE the repo, e.g. your domain names) so the checker itself names no site.
# Allowed on purpose: documentation placeholders (example.com/.test, 10.20.30.40-style TEST
# addresses in 192.0.2.0/24 and the RFC1918 NETWORK names used by validation code, see ALLOW).
# Public source digests are allowlisted for complete Git-history auditing.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
PATTERNS=(
  '-----BEGIN [A-Z ]*(PRIVATE KEY|CERTIFICATE)-----'
  '(^|[^0-9a-fA-F])[0-9a-f]{64}([^0-9a-fA-F]|$)'
  '(^|[^0-9.])(10\.[0-9]{1,3}|192\.168|172\.(1[6-9]|2[0-9]|3[01]))\.[0-9]{1,3}\.[0-9]{1,3}([^0-9]|$)'
  '/dev/(cu|tty)\.usb(modem|serial)[0-9]+'
  '/Users/[A-Za-z0-9._-]+'
  'eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.'
)
# Known-safe literals (test vectors / network names / placeholders), matched per line.
ALLOW='(component_hash:|manifest_hash:|0{63}1|10\.0\.0\.0/8|192\.168\.0\.0/16|172\.16\.0\.0/12|10\.20\.30\.40|10\.0\.0\.[0-9]+|10\.99\.0\.[0-9]+|/Users/you|# allow-secret-scan|QUICKJS_SOURCE_SHA256|SHA256|sha256|e3b0c442|ba7816bf|248d6a61|cdc76e5c|479ad1dc|36128da1)'
if [[ -n "${WAVESHARE_AI_DENYLIST:-}" && -f "$WAVESHARE_AI_DENYLIST" ]]; then
  while IFS= read -r word; do [[ -n "$word" && "$word" != \#* ]] && PATTERNS+=("$word"); done < "$WAVESHARE_AI_DENYLIST"
fi
fail=0
hits_file=$(mktemp "${TMPDIR:-/tmp}/check-no-secrets.XXXXXX")
trap 'rm -f "$hits_file"' EXIT
scan() {  # $1 = label, $2 = rev (empty = index); appends non-allowed hits to $hits_file
  local p
  for p in "${PATTERNS[@]}"; do
    if [[ -z "$2" ]]; then
      git grep -nIE --cached -e "$p" -- . ':!third_party' ':!tools/check-no-secrets.sh' 2>/dev/null || true
    else
      git grep -nIE -e "$p" "$2" -- . ':!third_party' ':!tools/check-no-secrets.sh' 2>/dev/null || true
    fi
  done | { grep -Ev "$ALLOW" || true; } | cut -c1-160 | sed "s/^/[$1] /" >> "$hits_file"
}
scan tree ""
if [[ "${1:-}" == --history ]]; then
  for rev in $(git rev-list --all); do scan "${rev:0:8}" "$rev"; done
  # Firmware images, dumps and key material are never allowed anywhere in history.
  git rev-list --all --objects | awk 'NF>1{print $2}' | { grep -Ei '\.(bin|elf|pem|key|crt|p12)$' || true; } \
    | sed 's/^/[history-file] /' >> "$hits_file"
fi
if [[ -s "$hits_file" ]]; then sort -u "$hits_file"; fail=1; fi
if [[ $fail -ne 0 ]]; then echo "check-no-secrets: FAIL" >&2; exit 1; fi
echo "check-no-secrets: OK (${1:-tree})"
