# Waveshare ESP32-S3-Touch-AMOLED-1.8 V2

This is the only supported device target. It has a 368×448 AMOLED touch display, ESP32-S3,
on-board battery/power management, microphone and speaker. V1 is untested; ESP32-C6 is unsupported.
The ESP-IDF 6.0.2 project is in [`firmware/`](firmware/).

## Install

Install ESP-IDF 6.0.2 and its Python environment, then from the repository root:

```sh
cp .env.example .env
chmod 600 .env
# Add private 2.4 GHz Wi-Fi values and select providers.
NO_FLASH=1 ./dev.sh
tools/flash.sh
```

`IDF_PATH` defaults to `../esp-idf-6.0.2`. `IDF_PYTHON_ENV_PATH` may select an already provisioned
IDF Python. Build output and generated device presets stay inside this device's ignored
`firmware/build/`; tracked `sdkconfig.defaults` and `dependencies.lock` remain the source inputs.

Select a build without editing tracked configuration:

```sh
PROVIDERS=none NO_FLASH=1 ./dev.sh                   # core only
PROVIDERS=hermes NO_FLASH=1 ./dev.sh                 # Sparkles + Ask
PROVIDERS=home_assistant NO_FLASH=1 ./dev.sh         # Sensor
PROVIDERS=hermes,home_assistant NO_FLASH=1 ./dev.sh  # all tiles
```

`WAVESHARE_AI_HERMES_TILES=sparkles`, `ask`, or `sparkles,ask` narrows the Hermes tiles.
`WAVESHARE_AI_AUTO_SLEEP_MIN=0..120` controls idle battery sleep; 0 disables it. Rebuild and flash
after changing a device/plugin build setting.

## Wi-Fi and flashing

Put the home network in `WAVESHARE_AI_WIFI_*` and an optional fallback hotspot in
`WAVESHARE_AI_HOTSPOT_*`. Fill both fields of a network or leave both empty. Keep `.env` mode 0600;
do not put credentials in shell arguments, logs or Git. The flash tool sends them over USB only
after a successful flash, and they are not part of the firmware image.

```sh
PORT=/dev/your-device tools/flash.sh  # select one board when several are attached
WIFI=0 tools/flash.sh                 # flash without provisioning networks
MONITOR=1 tools/flash.sh              # flash, then open the IDF monitor
export IDF_PATH="${IDF_PATH:-$PWD/../esp-idf-6.0.2}"
. "$IDF_PATH/export.sh"
python tools/set_wifi.py --env .env   # update Wi-Fi without rebuilding
python tools/set_wifi.py --forget     # logically forget saved networks
```

If USB does not enumerate, hold **PWR** for 5 seconds to turn off, hold **BOOT**, press **PWR**,
then release **BOOT** and flash. USB is only for flashing/debugging; operation uses Wi-Fi/battery.
Reflashing preserves NVS, including networks, device identity and enrollment. NVS is **unencrypted**;
forgetting is logical deletion, not a physical credential wipe.

## Controls and power

| Action | Result |
|---|---|
| Swipe sideways on Home, then tap | choose and open a tile |
| Pull upward from the bottom edge | reveal Home through the centered expanding circle; release near center or flick upward to commit |
| Sparkles touch/sideways swipe | interact with activity; temporarily change its palette |
| Hold Ask, then release | listen, work and show the reply; tap during work to stop |
| Swipe sideways in Settings | change account, display, sound and battery tabs |
| Press **PWR** or **BOOT** | sleep or wake |
| Hold **PWR** for 5 seconds | power off; press **PWR** to turn on |

Battery auto-sleep does not interrupt Ask recording/work. Settings offers Light/Dark plus Orange,
Blue, Green, Pink and Purple accents. Plugin account/enrollment controls are documented in the
[Hermes](../../plugins/hermes/README.md) and [Home Assistant](../../plugins/home_assistant/README.md)
installation guides.
