# Providers (main/Kconfig.projbuild, "Waveshare AI providers" in idf.py menuconfig) -> the explicit 0/1
# switches plugins.h reads (host tests pass the same -D flags). The tiles follow their provider:
# Sparkles/Ask = Hermes provider && its tile option, Sensor = the Home Assistant provider.
set(waveshare_PROVIDER_HERMES 0)
set(waveshare_PROVIDER_HOME_ASSISTANT 0)
set(waveshare_PLUGIN_SPARKLES 0)
set(waveshare_PLUGIN_AI 0)
set(waveshare_PLUGIN_HOME_ASSISTANT 0)
if(CONFIG_WAVESHARE_AI_PROVIDER_HERMES)
 set(waveshare_PROVIDER_HERMES 1)
 if(CONFIG_WAVESHARE_AI_HERMES_SPARKLES)
  set(waveshare_PLUGIN_SPARKLES 1)
 endif()
 if(CONFIG_WAVESHARE_AI_HERMES_ASK)
  set(waveshare_PLUGIN_AI 1)
 endif()
endif()
if(CONFIG_WAVESHARE_AI_PROVIDER_HOME_ASSISTANT)
 set(waveshare_PROVIDER_HOME_ASSISTANT 1)
 set(waveshare_PLUGIN_HOME_ASSISTANT 1)
endif()
# Each provider's own firmware lives in its plugin folder (plugins/<name>/firmware, see plugins/README.md);
# this one main component builds them: the folders are always on the include path (the device core's
# headers name the plugin headers) and a plugin's .c files are compiled only when its switch is on.
get_filename_component(waveshare_PLUGINS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../plugins" ABSOLUTE)
set(waveshare_INCLUDE_DIRS "." "${waveshare_PLUGINS_DIR}/hermes/firmware" "${waveshare_PLUGINS_DIR}/home_assistant/firmware")
# The display owner (direct_main.c) draws native RGB565 frames itself; core = Home, Settings, Wi-Fi,
# Battery, power button. Any provider talks to the bridge host.
set(owner_source "direct_main.c" "home_wifi.c" "pmu_diag.c" "power_pmu.c" "power_main.c")
if(waveshare_PROVIDER_HERMES OR waveshare_PROVIDER_HOME_ASSISTANT)
 list(APPEND owner_source "home_live.c" "home_pair.c" "pair_tls.c")  # bridge host: pairing, sign-in, feeds
endif()
if(waveshare_PLUGIN_AI)
 list(APPEND owner_source "${waveshare_PLUGINS_DIR}/hermes/firmware/helper_voice.c")
endif()
idf_component_register(SRCS "${owner_source}" INCLUDE_DIRS ${waveshare_INCLUDE_DIRS} REQUIRES esp_driver_i2c qrcodegen esp_timer esp_psram esp_app_format esp_wifi esp_event esp_netif esp_http_client tcp_transport mbedtls bootloader_support espressif__mdns nvs_flash vfs esp_driver_usb_serial_jtag esp_hal_usb espressif__esp_lcd_touch espressif__esp_codec_dev waveshare__esp32_s3_touch_amoled_1_8)
# Disclosed, build-gated test build (default OFF, not in the shipped firmware). -DVOICE_SELFTEST=ON
# (`VOICE_SELFTEST=ON ./dev.sh`) accepts the CRC-checked WPK1 USB frames that replay power-key
# presses, sleep and wake without touching the button (tests/board/power_probe.py); an injected
# hold never powers the board off.
if(VOICE_SELFTEST)
 target_compile_definitions(${COMPONENT_LIB} PRIVATE HELPER_VOICE_SELFTEST=1)
endif()
# plugins.h reads these explicit 0/1 switches (host tests pass the same -D flags).
foreach(switch PROVIDER_HERMES PROVIDER_HOME_ASSISTANT PLUGIN_SPARKLES PLUGIN_AI PLUGIN_HOME_ASSISTANT)
 target_compile_definitions(${COMPONENT_LIB} PRIVATE WAVESHARE_AI_${switch}=${waveshare_${switch}})
endforeach()
# Battery auto sleep minutes (Kconfig "Waveshare AI device"; sleep_policy.h, default 0 = never).
if(DEFINED CONFIG_WAVESHARE_AI_AUTO_SLEEP_MIN)
 target_compile_definitions(${COMPONENT_LIB} PRIVATE WAVESHARE_AI_AUTO_SLEEP_MIN=${CONFIG_WAVESHARE_AI_AUTO_SLEEP_MIN})
else()
 target_compile_definitions(${COMPONENT_LIB} PRIVATE WAVESHARE_AI_AUTO_SLEEP_MIN=0)
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
# IDF writes __FILE__ of project sources as ./main/...; give shared plugin sources a stable
# repository-relative prefix too (../../../../plugins/... from this named device project).
# so no checkout path ends up in the firmware.
target_compile_options(${COMPONENT_LIB} PRIVATE "-fmacro-prefix-map=${waveshare_PLUGINS_DIR}=../../../../plugins")
