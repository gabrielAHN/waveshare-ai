# Plugins

Plugins are compile-time additions to the Waveshare AI device core. The core remains the only
owner of display presentation, input, Wi-Fi, TLS transport, power and Settings.

| Plugin | Adds | Build selection | Service guide |
|---|---|---|---|
| [Hermes](hermes/README.md) | Sparkles and Ask | default, or `WAVESHARE_AI_PROVIDERS=hermes` | [install](hermes/README.md#install) |
| [Home Assistant](home_assistant/README.md) | Sensor | `WAVESHARE_AI_PROVIDERS=home_assistant` | [install](home_assistant/README.md#install) |

Use `WAVESHARE_AI_PROVIDERS=hermes,home_assistant` for both or `none` for core only, then rebuild
and flash. The bridge enables a provider when its key is present in `bridge.json`; firmware and
bridge selections should agree. `waveshare-bridge provider list` reports the configured tiles and
whether providers share a phone sign-in.

The pictures in each plugin's `images/` directory are firmware host renders, not photos. See the
[device setup](../devices/waveshare-esp32-s3-touch-amoled-1-8-v2/README.md), [architecture](../docs/ARCHITECTURE.md), and each plugin's
local setup for its service configuration.
