# waveshare-bridge

`waveshare-bridge` is the board's only network peer. It owns certificate-pinned enrollment, phone
approval, per-provider groups, provider machine credentials and the authenticated Hermes gadget
front. It runs on macOS or Linux with Python 3.11+.

Install the distribution in a dedicated environment:

```sh
python3 -m venv ~/.local/share/waveshare-bridge/venv
~/.local/share/waveshare-bridge/venv/bin/pip install -e ./bridge
export PATH="$HOME/.local/share/waveshare-bridge/venv/bin:$PATH"
```

Service initialization and authentication belong to the plugin being installed:

- [Hermes setup](../plugins/hermes/SETUP.md)
- [Home Assistant setup](../plugins/home_assistant/SETUP.md), which does not require Hermes

## Commands

| Command | Supported behavior |
|---|---|
| `init [--provider hermes\|home_assistant] [--launchd] [--label NAME] [--force]` | create one provider's bridge/TLS configuration; Hermes is the default, launchd is macOS-only |
| `run` | serve the configured providers, board API, discovery and local control socket |
| `enroll [--seconds N]` | open an enrollment window and confirm the comparison code |
| `status` | show non-secret configuration, certificate fingerprint and enrolled boards |
| `boards [--remove ID \| --revoke-phone ID]` | list/revoke boards or clear their phone approvals |
| `provider list` | list provider tiles, sign-in gateway keys and sharing |
| `plugin install [--source DIR] [--hermes-home DIR] [--issuer URL --jwks-uri URL]` | install Hermes dashboard integration and non-secret settings |
| `hermes sdk-setup --bot NAME=PORT [--bot ...] [--port N] [--apply] [--skip-install]` | print or apply pinned stock SDK setup |

Global `--config-dir DIR` or `WAVESHARE_AI_CONFIG` selects the private configuration directory.
SDK setup is print-only without `--apply`; it does not restart gateways or the bridge.

## Configuration

The `waveshare_bridge.config` loader rejects unknown keys. A provider is enabled by its presence
under `providers`; unserved routes return 404 before authentication and do not start upstream work.
[Placeholder examples](examples/README.md) exercise the actual schema.

| Location | Accepted keys |
|---|---|
| top level | `bind`, `port`, `name`, `mdns`, `allow_ip`, `providers` |
| `providers.hermes` | `gateway`, `client_file`, `tiles`, `bots`, `bot_names`, `bot_providers`, `profiles_dir`, `quota_cache`, `sign_in`, `gadget_sdk`, `sparkles` |
| `providers.hermes.gadget_sdk` | `port`, and `profiles` mapping ids to distinct loopback ports |
| `providers.hermes.sparkles` | `level_thresholds`, `usage_interval`, `ema_seconds` |
| `providers.home_assistant` | `client_file`, `sign_in` |
| each `sign_in` | `client_id`, `groups`, `issuer`, `oidc_paths`, `host_header`, `client_file`; `required` is valid only for Hermes |
| `sign_in.oidc_paths` | `device_authorization`, `token`, `userinfo` |

Hermes supports `sparkles` and `ask`; Home Assistant supports `sensor`. Providers share one phone
flow only when their resolved issuer, public client id, all endpoint paths and Host header match.
Each provider applies its own groups. Sign-out clears every provider sharing that gateway.
Home Assistant phone authorization is always required; Hermes alone may set `required` false.
Approval is durable local state rather than continuous userinfo polling, so approve again after group changes.

Default private directory: `~/.config/waveshare-ai/`.

| File | Contents | Mode |
|---|---|---|
| `bridge.json` | service and provider settings | 0600 |
| `authelia-client.json` | Hermes machine client, usage/capability scopes and audience | 0600 |
| `home-client.json` | sensor proxy machine client and resource URL | 0600 |
| `tls.crt`, `tls.key` | bridge leaf certificate and key | 0644 / 0600 |
| `board.key` | session-pseudonym HMAC key | 0600 |
| `boards.json` | enrolled identities and phone/group snapshots | 0600 |
| `gadget-grants.json`, `quota-blocks.json` | bridge-owned SDK approvals and verified quota latches | 0600 |
| `control.sock` | local enrollment control socket | 0600 |

Phone OAuth tokens stay in memory only during approval. Separate client-credentials tokens serve
Hermes metadata and the Home Assistant auth proxy. The board receives none of those credentials.

## Network surface

| Route | Contract |
|---|---|
| `POST /v1/enroll` | WEN1 enrollment during an operator-opened window |
| `GET /v1/live` | WLS4 session metadata |
| `GET /v1/bots` | WBT1 profile capability/quota/phone gate |
| `GET /v1/home` | WHS1 Home Assistant sensor data |
| `/v1/pair/phone/{start,status,forget}` | provider-specific WPH1 device authorization |
| `wss://HOST:8768/gadget/BOT` | authorized board to configured loopback stock SDK listener |

Phone routes require `X-Provider: hermes` or `home_assistant`. The gadget front removes the board's
Authorization header before connecting upstream. Protocol details are in
[Architecture](../docs/ARCHITECTURE.md); trust boundaries are in [Security](../docs/SECURITY.md).

## Tests

```sh
cd bridge
PYTHONPATH="$PWD" python -m unittest discover -s tests -t .
```

Tests use temporary credentials and controlled providers. mDNS cases require an RFC1918 interface;
`WAVESHARE_AI_TEST_LAN_IP` selects one and cases skip without it. Test quota/profile/cache/gate paths
must remain explicit isolated fixtures; do not use a real profile or refresh a provider quota.
