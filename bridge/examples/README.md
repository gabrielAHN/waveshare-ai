# Bridge examples

These files contain only `example.com`, loopback and `10.0.0.x` placeholders. Copy only what your
provider needs into `~/.config/waveshare-ai/` (directory mode 0700; JSON and key files mode 0600),
replace placeholders locally, and never commit the private copies.

| Example | Purpose |
|---|---|
| [bridge.example.json](bridge.example.json) | complete strict `bridge.json` schema with both providers and one shared public phone client |
| [authelia-client.example.json](authelia-client.example.json) | Hermes dashboard machine client |
| [home-client.example.json](home-client.example.json) | Home Assistant proxy machine client and exact resource URL |
| [home-assistant-sensor-adapter.py](home-assistant-sensor-adapter.py) | loopback adapter that owns the Home Assistant API token and emits the sensor contract |
| [sensor-endpoint.example.json](sensor-endpoint.example.json) | six-reading response consumed by the bridge |

The combined bridge example retains the scripted default profile ids used by repository renders;
replace them with your configured Hermes profiles. Both provider sign-ins resolve
`https://auth.example.com`, `waveshare-pairing` and the same endpoint paths, so they share one QR
while preserving their separate group lists.

The Authelia examples were tested with exactly version 4.39.28. The Home Assistant machine-client
ACL relies on the ClientID-aware authorization fix verified in that version.

For complete service instructions, use the [Hermes setup](../../plugins/hermes/SETUP.md) or the
[standalone Home Assistant setup](../../plugins/home_assistant/SETUP.md). The bridge
[configuration reference](../README.md#configuration) lists accepted keys.

`bridge/tests/test_examples.py` validates the JSON examples with the real loaders. Run the
publication scanner with `WAVESHARE_AI_DENYLIST` pointing to your private denylist; suppress scanner
hits when their text may expose private values.
