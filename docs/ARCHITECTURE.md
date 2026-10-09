# Architecture

Waveshare AI is an **ESP-IDF device core plus Hermes and Home Assistant plugins**, connected to one
service on the bridge host. The bridge owns board enrollment and phone/group authorization.
The dashboard plugin supplies session usage and bot capabilities; **Ask voice uses the stock
Gadget SDK in each profile's Hermes gateway**. [Device setup](../devices/waveshare-esp32-s3-touch-amoled-1-8-v2/README.md#install) ·
[Hermes setup](../plugins/hermes/SETUP.md) · [Home Assistant](../plugins/home_assistant/SETUP.md) ·
[Security](SECURITY.md).

## Components and build

| Component | Responsibilities |
|---|---|
| `devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main/direct_main.c` | one RGB565 display/presenter owner; touch snapshots and rendering |
| Core `home_ui.h`, `home_render.h`, `theme.h` | Home carousel, Settings, gesture arbitration, Light/Dark + five accents |
| Core `home_wifi.c`, power code | USB-provisioned Wi-Fi, NVS, sleep/wake/off, battery diagnostics |
| Core `home_pair.c`, `pair_state.h`, `pair_tls.c` | on-device identity, discovery, enrollment and certificate-pinned transport |
| Core `home_live.c` | serialized pairing/live/bot/phone/Sensor HTTPS worker |
| `plugins/hermes/firmware/` | Sparkles, WLS4, Ask UI/audio/SDK wire, bot gates and Kotaro |
| `plugins/home_assistant/firmware/` | Sensor WHS1 decoder and model |
| `bridge/waveshare_bridge/` | strict configuration, enrollment registry, phone device grant, WLS4/WBT1/WHS1, gadget front |
| `plugins/hermes/dashboard-plugin/` | scoped JWT authentication for only `/usage` and `/bots` |
| External stock SDK per profile | loopback gadget platform, SDK authentication/audio, Hermes speech/agent turn and replies |

The named device's `firmware/` directory is an ESP-IDF **6.0.2** project with one main component. Its CMake file includes
both plugin firmware folders and compiles provider sources according to Kconfig. There is one
native display owner, not a display stack per plugin. The BSP and mDNS are pinned in
`devices/waveshare-esp32-s3-touch-amoled-1-8-v2/firmware/main/idf_component.yml` and the adjacent
`dependencies.lock`.

`CONFIG_WAVESHARE_AI_PROVIDER_HERMES` (default y) includes `CONFIG_WAVESHARE_AI_HERMES_SPARKLES`
and `CONFIG_WAVESHARE_AI_HERMES_ASK` (default y).
`CONFIG_WAVESHARE_AI_PROVIDER_HOME_ASSISTANT` defaults to n. `WAVESHARE_AI_PROVIDER_*` and derived
`WAVESHARE_AI_PLUGIN_*` definitions feed the named device's `firmware/main/plugins.h`.
Home tiles are registered in `tile_plugins.h`; Settings is last. A missing tile's page resolves
to Home. A core-only build has no provider network worker. [Plugin layout](../plugins/README.md).

## Data flow

```text
board -- pinned HTTPS --> bridge -- scoped machine token --> dashboard /usage, /bots
   |                          |-- separate bearer --> auth proxy --> sensor endpoint --> Home Assistant
   |                          |-- device grant/userinfo --> OIDC gateway <-- phone login + consent
   |-- pinned WSS /gadget/bot -> gadget front --> selected profile's loopback stock SDK --> Hermes turn
```

1. **Sparkles:** bridge `/v1/live` always returns WLS4. The dashboard usage feed reads open-session
   metadata using `session.active_list`; optional bounded `session.status` sampling adds token
   counters and canonical provider evidence, never message contents or account usage calls.
   IDs become HMAC pseudonyms with the private `board.key` and the fixed domain label defined in
   `live_bridge.py`. Open sessions have a glow; idle ones are softer, working ones brighter with
   a star. Ambient density follows **working-session count**. Token-density metadata is carried
   in the frame but is not the device's density policy. Unknown provider evidence stays neutral;
   Anthropic, OpenAI Codex and OpenRouter have explicit codes. Stale/failed/empty live evidence
   clears automatic activity; touch sparkles remain independent. Device live freshness is 10 s.
2. **Ask:** a stationary hold within the bot control arms after **400 ms**. The ES8311 captures
   16 kHz PCM16; firmware drops the initial settling samples and conditions speech gain.
   The board authenticates to `wss://<bridge>:8768/gadget/<bot>` with the same certificate pin.
   The front checks its enrolled identity, resolved Hermes phone authorization and bot map,
   writes the stock SDK pairing grant, strips the board bearer from the upstream upgrade and
   carries the SDK protocol to `127.0.0.1:PORT/gadget` in that profile's gateway.
   The adapter handles speech and the agent turn; transcript/reply/status return to the board.
   New chat sends `session.new` before the next utterance; Stop sends `cancel`.
   [SDK contract](../plugins/hermes/GADGET-SDK.md).
3. **Bots and quota:** `/v1/bots` uses dashboard `/bots` capabilities, local profile `model.provider`
   and optional official quota telemetry. Explicit exhaustion is latched until fresh usable
   recovery, even across telemetry errors/restarts. Missing telemetry or a usage check's own
   missing login does not alone block a routable bot. A down dashboard reports unavailable.
   The board blocks recording on a reported quota/phone/upstream gate; a quota reset countdown
   alone cannot unlock it. The gadget front does not inspect quota or agent commands.
   Sideways swipes choose up to three profiles; selection persists and each conversation remains.
4. **Sensor:** `/v1/home` checks Home Assistant phone/group authorization, then fetches the
   configured endpoint through its bearer auth proxy. The endpoint keeps the Home Assistant
   credential and supplies text-valued readings; missing/non-numeric data shows `--`.
   The bridge grades the readings into WHS1, with a 5 s upstream cache. The board polls every
   10 s while Sensor is visible. [Endpoint and client](../plugins/home_assistant/SETUP.md).

The core HTTPS worker serializes its TLS operations; Ask has its own SDK connection.
Live polling pauses on Ask (`phase=6`) and audio work coordinates with in-flight polls.
The firmware's TCP MSS is 1360 and TLS input buffer is 16 KiB. Network calls, NVS operations,
touch sampling and display presentation retain their separate task/ownership boundaries.

## Enrollment

The board creates 32 random identity bytes on first boot and stores them in NVS. Discovery uses
mDNS `_waveshare-ai._tcp` with bridge name, port and SHA-256 certificate fingerprint. Choosing a
bridge pins its leaf before submitting WEN1. An explicit WLB2 USB candidate supplies the same
address/pin without approving it.

```text
host opens window: waveshare-bridge enroll
board pins discovered fingerprint, POST /v1/enroll with WEN1 + identity + name
both show SHA-256(domain || fingerprint || SHA-256(identity))[0:4] mod 10^6
operator compares codes: y -> accepted; n -> denied
board persists PAR1 enrollment on acceptance
```

The enrollment domain bytes are `waveshare-ai-enroll-v1` followed by NUL; digest bytes are read
big-endian for the six-digit code. HTTP outcomes: 200 accepted, 202 pending, 400 malformed,
403 closed window, 409 denied, 410 expired, 429 busy/rate-limited. Denial, expiry or cancellation
after submission rotates the identity. Only the host operator confirms; USB hooks cannot.

## Device states

Persisted `pair` record (`PAR1`) stores state, bridge base/name and certificate pin.

| State | Meaning |
|---|---|
| `no_bridge` (0) | no trusted bridge |
| `enrolling` (1) | transient enrollment request |
| `enrolled_unpaired` (2) | enrolled board; phone authorization comes from bridge status |
| `paired` (3) | reserved record state; UI authorization is resolved from phone status |

Home tiles are ON, LOADING or OFF, with a reason instead of a speculative failure while checks
are pending. Pulling **up from the bottom 28 px** (`y >= 420`) toward screen centre `(184,224)`
reserves Home only after upward travel wins; buffered non-Home input keeps its original timestamps
and hold timing. A circle fixed at physical panel centre expands with upward distance, revealing the
actual Home frame inside while the untransformed outgoing page remains outside. The saved semantic
accent appears only on the narrow antialiased edge. Releasing at centre, or after an upward flick of
at least **24 px** at **0.5 px/ms**, commits Home; a partial flick reaches diagonal corner cover within
**240 ms**, while a slow partial pull contracts to zero and restores the exact page within **220 ms**.
Tile OPEN keeps its existing slide. The presenter, outgoing snapshot and Home cache retain single-owner safety.

NVS `home_wifi` stores Wi-Fi copy-on-write slots (`station`, `station2`, `wifi_slot`), `hotspot`,
`device_id`, session/completion preferences, `theme` and selected `bot`. Theme stores Light/Dark
and one of Orange, Blue, Green, Pink or Purple. Battery sleep turns off panel/touch/audio/Wi-Fi;
wake restores the page and reconnects USB after light sleep. PWR press sleeps/wakes, hold 5 s
powers off; BOOT press sleeps/wakes. [Power behavior](../devices/waveshare-esp32-s3-touch-amoled-1-8-v2/README.md#controls-and-power).

## Phone sign-in: device authorization grant

Each phone request **requires** `X-Provider: hermes` or `home_assistant`. Missing, unknown and
unserved providers answer **404**. Start requests a public client's device code; the board shows
the verification URL/code as a QR. The phone completes gateway login, 2FA and consent. The bridge
polls the token endpoint, reads userinfo groups/name, records approval and discards the tokens.
It respects `slow_down`; verification URLs must be HTTPS on the configured gateway host.

Providers resolving to identical issuer, public client id, endpoint paths and Host header share
**one** flow, QR and account tab. Each status still applies that provider's own group allowlist.
A user outside it receives refused; a shared sign-out clears that gateway for both providers.
`boards --revoke-phone ID` clears all approvals. Groups are snapshots at approval time.

Registry `boards.json` stores Hermes gateway approval in `phone_user`, `phone_name`, `phone_at`,
`phone_groups`; other gateways use `sign_ins: {key: {user, name, at, groups}}`.
The gateway key is the first 16 hex digits of SHA-256 over
`issuer|client_id|device_authorization|token|userinfo|host_header`. No OAuth token is persisted.

## Wire formats

Integers are little-endian except the enrollment comparison calculation. Length, version,
reserved-field and flag checks are fail-closed. CRC-32 protects the frames that carry it;
TLS and board authorization provide the network trust boundary.

| Format | Layout / meaning |
|---|---|
| **WLS4** | 8-byte header: magic, u16 count (0–128), u8 level (0–5), u8 flags; count × {u64 nonzero ascending unique pseudonym, u8 provider, u8 state (bit 0 working)}; no CRC |
| **WBT1** | 144 bytes: header (version 1, count 0–3, flags), three fixed slots {id[12], name[12], provider[12], available, reason, stale flags, reserved, u32 reset seconds}, then CRC-32 |
| **WPH1** | 228 bytes: version, state, flags, expiry; NUL-padded code[16], name[33], URI[163]; CRC-32 |
| **WEN1** | 69-byte enrollment request: magic + identity[32] + NUL-terminated name[33] |
| **WHS1** | 48 bytes: header, six {i16 value ×10, grade, reserved, u16 age} slots, CRC-32 |

WLS4 provider codes: 0 unknown, 1 Anthropic, 2 OpenAI Codex, 3 OpenRouter. Flags are 1 measured,
2 degraded. State has only the working bit; with no working sessions, level is 0.
WBT1 emitted reasons: 0 available, 1 exhausted, 2 provider sign-in, 4 phone authorization,
5 upstream unavailable. Unknown reasons display generic unavailable text; reason numbering and
the [shared golden fixture](../bridge/tests/fixtures/bots_frame.hex) are stable.
WPH1 states: none, pending, authorized, denied, expired, refused, error. Its only known flags
are **1 required** and **8 shared** (mask 9); shared is set whenever that provider shares sign-in.

The API serves `/v1/enroll`, `/v1/live`, `/v1/bots`, `/v1/home` and
`/v1/pair/phone/{start,status,forget}`, plus the WSS gadget front. The dashboard plugin's two
routes are `/api/plugins/waveshare-sessions/usage` and `/api/plugins/waveshare-sessions/bots`.

## USB frames and tests

| Frame | Availability | Purpose |
|---|---|---|
| WSP1 | 90 s boot provisioning window | home/hotspot credentials; network selector 0 / 1 |
| WLV1 | boot provisioning window, one-shot page selection | select a built page |
| WVC1 | any time | Ask press/release/Stop test input |
| WBS1 | any time | next / previous bot |
| WGS1 | any time | chat, Home pull, tile opening and Sparkles gesture replay/report |
| WPC1 | any time | scan, cancel, forget bridge, phone QR, first/second target, forget Wi-Fi, pick row |
| WLB2 | any time | explicit bridge address + public certificate pin |
| WPK1 | `VOICE_SELFTEST` build only | disclosed power-key replay/report |

Simple actions use magic + u8 payload + CRC-32(payload); larger setup frames carry bounded
structures and CRC. Inputs are logged as `source=usb_serial`, never physical touches. They cannot
approve enrollment. [Host/preview/device tests](../README.md#repository) ·
[bridge tests](../bridge/README.md#tests) · [dashboard tests](../plugins/hermes/dashboard-plugin/README.md#tests).
