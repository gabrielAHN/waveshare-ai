# Ask voice: stock Gadget SDK

Ask uses the unmodified `gadget` platform from
[`Adolanium/hermes-gadget-sdk`](https://github.com/Adolanium/hermes-gadget-sdk), installed
externally into **each bot profile's Hermes messaging gateway**. The bridge's setup command pins
revision `75b8a689bce3925d46de5c256f53d343ae6b876f`. The SDK is not stored in this repository.

## Path

```text
Ask on the board (hold 400 ms, speak, release)
  wss://<bridge>:8768/gadget/<bot>  — enrolled bridge certificate pin + board bearer
    -> bridge gadget front: enrolled board + Hermes phone authorization + configured bot
       approve its SDK identity in that profile's gadget pairing store
       connect the upgrade without the board Authorization header
    -> selected profile's stock gadget platform on 127.0.0.1:PORT/gadget
       hello / challenge / auth / welcome
       audio.start / 16 kHz PCM16 / audio.end
       Hermes speech transcription -> that profile's agent
       status / transcript / reply.delta / reply / turn.end -> board text
```

`providers.hermes.gadget_sdk` in `bridge.json` maps bots to distinct loopback ports. The front
uses the same certificate as the HTTPS board API. SDK keys are derived on the board with
HMAC-SHA256 of its device identity and the domain label `waveshare-ai-gadget-key-v1`.
The stock SDK authentication stays end-to-end between the board and adapter.

## Setup

Follow [Hermes setup](SETUP.md#4-configure-ask-voice):

```sh
waveshare-bridge hermes sdk-setup --bot helper=8775 --bot atlas=8776 --bot coding=8777
# Review the printed per-profile steps, then install/configure them:
waveshare-bridge hermes sdk-setup --bot helper=8775 --bot atlas=8776 --bot coding=8777 --apply
```

The command installs the pinned plugin, sets host **127.0.0.1**, each port, path `/gadget`,
`speak_replies=false`, `auto_home=true` and `unauthorized_dm_behavior=pair` before enabling it.
`--skip-install` omits installation; `--port N` sets the front port (default 8768), which must
match firmware `CONFIG_WAVESHARE_AI_GATEWAY_PORT`. Restart each profile's gateway and the bridge.
Keep `providers.hermes.bots`, its SDK profile map and dashboard `command_profiles` aligned.

## Who is allowed

- The front rejects an unknown board with **401**, an unauthorized board with **403**, and an
  unconfigured bot with **404**. A stopped profile gateway answers **503**.
- Phone approval is through the bridge's Hermes OIDC device grant and its allowed groups.
  The front writes the profile's `platforms/pairing/gadget-approved.json` and records its grants
  in private `gadget-grants.json`. There is no second human pairing step.
- A 5 s sweep revokes grants owned by the bridge when the board is removed or signs out;
  the stock adapter reports `unpaired`. Independently issued approvals are outside that ledger.
- `/v1/bots` provides the board's profile capability and quota gate. Explicit exhaustion disables
  recording with **No quota**; missing optional quota data is not itself a block. The front checks
  enrollment, phone authorization and its configured profile map; it does not interpret audio or quota.

## Conversation and replies

Swiping bots preserves each conversation. Pulling down from Ask's top, or pulling the newest
message up, starts a new chat: the board sends `session.new` before the next utterance (Hermes
`/new`). It hides the reset acknowledgement. Stop sends `cancel` (Hermes `/stop`). Approval
prompts are declined on the board; cancelled or failed turns are shown as such.

Replies are bounded text with a transcript and typed-out response, not spoken playback
(`speak_replies=false`). Connection loss is reported rather than replaying an utterance.
The dashboard plugin serves only usage and bot capabilities; it neither receives Ask audio nor
executes these turns. [Trust model](../../docs/SECURITY.md).

Host/protocol tests do not establish physical microphone quality, touch behavior, optical display
appearance or battery operation. Verify those separately on the board.
