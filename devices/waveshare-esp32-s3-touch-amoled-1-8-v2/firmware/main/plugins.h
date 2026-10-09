#pragma once
/* Build-time providers. The device core is always built: Home, Settings (Wi-Fi, Battery), Wi-Fi from
 * the flash (.env -> tools/flash.sh -> USB), display/touch, USB setup, the power button. A provider adds
 * Home tiles and ONE Settings tab, each with its own phone sign-in through the host bridge:
 *
 *   WAVESHARE_AI_PROVIDER_HERMES          Hermes: the Sparkles and Ask tiles, the Hermes Gadget SDK voice path
 *                                   and its sign-in; Settings > Hermes tab (title WAVESHARE_AI_HERMES_ACCOUNT_NAME)
 *   WAVESHARE_AI_PROVIDER_HOME_ASSISTANT  Home Assistant: the Sensor tile with its OWN sign-in; Settings > Home
 *                                   Assistant tab (title WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME). Opt-in.
 *
 * The tile switches are internal and follow the providers (one per Home tile; what each tile needs
 * from Hermes is its row in tile_plugins.h):
 *   WAVESHARE_AI_PLUGIN_SPARKLES        Sparkles page + tile; live Hermes session glows via the bridge (optional)
 *   WAVESHARE_AI_PLUGIN_AI              Ask page (hold-to-talk voice), bots, completion sound
 *   WAVESHARE_AI_PLUGIN_HOME_ASSISTANT  Sensor page (Home Assistant readings via the bridge)
 *
 * On the ESP32 they come from Kconfig (main/Kconfig.projbuild, `idf.py menuconfig` -> "Waveshare AI providers",
 * or CONFIG_WAVESHARE_AI_PROVIDER_* / CONFIG_WAVESHARE_AI_HERMES_* lines in an sdkconfig defaults file);
 * main/CMakeLists.txt passes every macro here as an explicit 0/1 (ESP32 default: Hermes on, Home
 * Assistant off). Host tests pass the same -D flags (tests/run_host_tests.sh HOST_TEST_DEFINES=...).
 * A missing definition = on, so the host suites cover both providers and every tile. */
#ifndef WAVESHARE_AI_PROVIDER_HERMES
#define WAVESHARE_AI_PROVIDER_HERMES 1
#endif
#ifndef WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
#define WAVESHARE_AI_PROVIDER_HOME_ASSISTANT 1
#endif
#ifndef WAVESHARE_AI_PLUGIN_SPARKLES
#define WAVESHARE_AI_PLUGIN_SPARKLES WAVESHARE_AI_PROVIDER_HERMES
#endif
#ifndef WAVESHARE_AI_PLUGIN_AI
#define WAVESHARE_AI_PLUGIN_AI WAVESHARE_AI_PROVIDER_HERMES
#endif
#ifndef WAVESHARE_AI_PLUGIN_HOME_ASSISTANT
#define WAVESHARE_AI_PLUGIN_HOME_ASSISTANT WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
#endif
/* A tile belongs to its provider: never build one without it. */
#if (WAVESHARE_AI_PLUGIN_SPARKLES || WAVESHARE_AI_PLUGIN_AI) && !WAVESHARE_AI_PROVIDER_HERMES
#error "WAVESHARE_AI_PLUGIN_SPARKLES and WAVESHARE_AI_PLUGIN_AI are Hermes tiles: they need WAVESHARE_AI_PROVIDER_HERMES=1"
#endif
#if WAVESHARE_AI_PLUGIN_HOME_ASSISTANT != WAVESHARE_AI_PROVIDER_HOME_ASSISTANT
#error "WAVESHARE_AI_PLUGIN_HOME_ASSISTANT is the Home Assistant provider's tile: switch WAVESHARE_AI_PROVIDER_HOME_ASSISTANT"
#endif
/* Anything that talks to the host bridge (pairing, phone sign-in, live worker): any provider or tile. */
#define WAVESHARE_AI_PLUGIN_BRIDGE (WAVESHARE_AI_PROVIDER_HERMES||WAVESHARE_AI_PROVIDER_HOME_ASSISTANT||WAVESHARE_AI_PLUGIN_SPARKLES||WAVESHARE_AI_PLUGIN_AI||WAVESHARE_AI_PLUGIN_HOME_ASSISTANT)
/* A phone sign-in gates only the Ask (Hermes) and Sensor (Home Assistant) tiles. */
#define WAVESHARE_AI_PLUGIN_SIGNIN (WAVESHARE_AI_PLUGIN_AI||WAVESHARE_AI_PLUGIN_HOME_ASSISTANT)
