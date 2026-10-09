#!/bin/bash
# Install the repo's waveshare-sessions plugin into a Hermes home, keeping its settings.json.
#
#   tools/install-live-plugin.sh                 # -> ~/.hermes/plugins/waveshare-sessions (the LIVE dashboard)
#   tools/install-live-plugin.sh --target DIR    # any plugin directory (dry runs, temp homes)
#   tools/install-live-plugin.sh --dry-run       # show what would change, touch nothing
#
# What it does:
#   1. backs up the whole existing plugin dir (settings.json included) to
#      ~/.hermes/waveshare-bridge/backups/waveshare-sessions.<timestamp>/  (or --backup-dir)
#   2. copies the plugin code (no tests/, no __pycache__) over the target
#   3. rewrites settings.json PRESERVING every existing key, and sets only
#      "command_profiles" (default: helper,atlas,coding; --profiles a,b,c) if not already present
#      (--force-profiles overwrites an existing list)
#   4. validates the result with the plugin's own settings loader
# It never restarts anything. The running dashboard keeps its loaded code until you restart it, e.g.
#   launchctl kickstart -k gui/$(id -u)/<your dashboard launchd label>   (HERMES_DASHBOARD_LABEL)
# Rollback: copy the backup dir back over the target and restart the dashboard the same way.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${HOME:?HOME must be set}"
SRC="$ROOT/plugins/hermes/dashboard-plugin"
TARGET="$HOME/.hermes/plugins/waveshare-sessions"
BACKUPS="$HOME/.hermes/waveshare-bridge/backups"
PROFILES="helper,atlas,coding"
DRY=0
FORCE=0
# Only used in the restart hint this script prints (it never restarts anything itself).
DASHBOARD_LABEL="${HERMES_DASHBOARD_LABEL:-<your dashboard launchd label>}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --target) TARGET="$2"; shift 2 ;;
    --backup-dir) BACKUPS="$2"; shift 2 ;;
    --profiles) PROFILES="$2"; shift 2 ;;
    --force-profiles) FORCE=1; shift ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -f "$SRC/plugin.yaml" && -f "$SRC/dashboard/plugin_api.py" ]] || { echo "plugin source missing: $SRC" >&2; exit 2; }
[[ -d "$TARGET" && -f "$TARGET/settings.json" ]] || { echo "no installed plugin with settings.json at $TARGET (use waveshare-bridge plugin install for a first install)" >&2; exit 2; }
PY="$(command -v python3)"
stamp="$(date +%Y%m%d-%H%M%S)"
backup="$BACKUPS/waveshare-sessions.$stamp"
echo "source : $SRC"
echo "target : $TARGET"
echo "backup : $backup"
echo "profiles: $PROFILES (force=$FORCE)"
work="$(mktemp -d "${TMPDIR:-/tmp}/wsh-install.XXXXXX")"
trap 'rm -f "$work/settings.json"; rmdir "$work"' EXIT
chmod 700 "$work"
"$PY" - "$TARGET/settings.json" "$PROFILES" "$FORCE" > "$work/settings.json" <<'EOF'
import json, re, sys
path, profiles, force = sys.argv[1], sys.argv[2].split(','), sys.argv[3] == '1'
data = json.load(open(path))
if not isinstance(data, dict):
    sys.exit('settings.json must be an object')
if not profiles or any(not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,10}', p) or p == 'default' for p in profiles):
    sys.exit('invalid --profiles: expected 1..11-character profile ids (not default)')
if force or 'command_profiles' not in data:
    data['command_profiles'] = profiles
print(json.dumps(data, indent=2))
EOF
"$PY" - "$TARGET/settings.json" "$work/settings.json" <<'EOF'
import json, sys
old = json.load(open(sys.argv[1])); new = json.load(open(sys.argv[2]))
allowed = {'command_profiles'}
lost = sorted(set(old) - set(new)); changed = sorted(k for k in old if k in new and old[k] != new[k] and k not in allowed)
if lost or changed:
    sys.exit('settings preservation failed')
print('settings keys kept:', ', '.join(sorted(old)))
print('command_profiles  :', new.get('command_profiles'))
EOF
if [[ "$DRY" == 1 ]]; then
  echo "dry run: nothing copied or written"
  exit 0
fi
# Validate the candidate against NEW source before touching the installed plugin.
WAVESHARE_SESSIONS_SETTINGS="$work/settings.json" "$PY" - "$SRC" "$work/settings.json" <<'EOF'
import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location('ws_candidate_check', pathlib.Path(sys.argv[1]) / 'core.py')
core = importlib.util.module_from_spec(spec); spec.loader.exec_module(core)
core.load_settings(pathlib.Path(sys.argv[2]))
print('candidate settings validated')
EOF
mkdir -p "$BACKUPS"
chmod 700 "$BACKUPS"
cp -Rp "$TARGET" "$backup"
cmp -s "$TARGET/settings.json" "$backup/settings.json" || { echo "backup verification failed" >&2; exit 1; }
rsync -a --exclude /vendor/ --exclude tests --exclude __pycache__ --exclude settings.json "$SRC/" "$TARGET/"
tmp="$TARGET/settings.json.tmp.$$"
cp "$work/settings.json" "$tmp"
chmod "$(stat -f %Lp "$backup/settings.json")" "$tmp"
mv "$tmp" "$TARGET/settings.json"
WAVESHARE_SESSIONS_SETTINGS="$TARGET/settings.json" "$PY" - "$TARGET" <<'EOF'
import importlib.util, sys, pathlib
spec = importlib.util.spec_from_file_location('ws_core_check', pathlib.Path(sys.argv[1]) / 'core.py')
core = importlib.util.module_from_spec(spec); spec.loader.exec_module(core)
s = core.load_settings(pathlib.Path(sys.argv[1]) / 'settings.json')
print('validated: command_profiles=%s command_scope=%s' % (','.join(s['command_profiles']), 'set' if s['command_scope'] else 'EMPTY'))
EOF
echo "Installed. Restart the dashboard to load it: launchctl kickstart -k gui/\$(id -u)/$DASHBOARD_LABEL"
echo "Rollback: rsync -a --delete '$backup/' '$TARGET/' && launchctl kickstart -k gui/\$(id -u)/$DASHBOARD_LABEL"
