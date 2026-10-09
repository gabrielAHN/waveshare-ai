# waveshare-sessions: Hermes dashboard plugin

The usage and bot-capability service for the [Hermes plugin](../README.md), running inside the
Hermes dashboard. Ask voice runs in each profile's **stock Gadget SDK gateway**, not here.

| Route under /api/plugins/waveshare-sessions | Purpose | Required scope |
|---|---|---|
| `GET /usage` | read-only open sessions; `?tokens=1` adds bounded token/provider metadata | `hermes.sessions.read` |
| `GET /bots` | configured profile capability ids from `command_profiles` | `hermes.helper.command` |

These are the plugin's two routes. Home Assistant does not use it.

## Install

Complete the [gateway and bridge setup](../SETUP.md), then:

```sh
waveshare-bridge plugin install
hermes plugins enable waveshare-sessions
```

The installer copies this folder to `$HERMES_HOME/plugins/waveshare-sessions` (default Hermes
home when unset) and writes non-secret `settings.json`. `--hermes-home DIR` selects the home,
`--source DIR` selects a source checkout, and `--issuer URL --jwks-uri URL` selects your gateway's
published JWT issuer and signing keys. Restart the dashboard yourself to activate it; restarting
ends its hosted chats. To refresh code while retaining installed settings, use
`tools/install-live-plugin.sh --dry-run`, then `tools/install-live-plugin.sh --profiles helper,atlas,coding`.
The script backs up, merges and validates settings without restarting services.

## Settings and authorization

| settings.json key | Meaning |
|---|---|
| `issuer`, `jwks_uri` | exact JWT issuer and signing-key endpoint |
| `audience`, `client_id` | plugin resource audience and authorized machine client |
| `scope` | usage scope (`hermes.sessions.read`) |
| `command_scope` | capability scope (`hermes.helper.command`); empty closes `/bots` |
| `command_profiles` | permitted profile ids returned by `/bots`; align with bridge bots and SDK profiles |
| `jwks_host_header`, `ca_file` | explicit host routing / private CA when needed |

Every request uses a machine JWT, not an interactive dashboard login. Verification pins RS256,
issuer, audience, client id, expiry/issued-at and the scope for the **exact request path**.
A JWKS outage fails closed. The token cannot authorize dashboard administration or chats.
The bridge is responsible for enrollment, phone approval and provider group checks.
[Client registration](../SETUP.md#machine-client-for-dashboard-metadata).

The usage feed reads `session.active_list` and, when token metadata is requested, bounded
`session.status` fields. It reads neither message text nor account usage APIs. Provider evidence
comes from the canonical `Model:` field, not a guessed model/profile mapping.

## Files

`plugin.yaml`, `__init__.py` (token provider registration), `core.py` (JWT verification and usage),
`dashboard/plugin_api.py` (usage and bot capability routes), `settings.example.json`.

## Tests

Run from a Hermes checkout with a disposable `HERMES_HOME` **outside the real Hermes home** so
route discovery cannot load the installed plugin. Use your own checkout/venv paths:

```sh
REPO=/path/to/waveshare-ai
cd /path/to/hermes-agent
HERMES_HOME=$(mktemp -d "$TMPDIR/waveshare-dashboard-test.XXXXXX") HERMES_DISABLE_LAZY_INSTALLS=1 \
  .venv/bin/python -m pytest "$REPO/plugins/hermes/dashboard-plugin/tests" -q -p no:cacheprovider
```

Tests do not establish profile gateway activation or a physical spoken turn.
[Troubleshooting](../SETUP.md#quota-and-troubleshooting).
