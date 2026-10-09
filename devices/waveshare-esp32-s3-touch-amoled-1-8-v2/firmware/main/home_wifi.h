#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "home_ui.h"
/* One low-priority worker owns NVS and Wi-Fi API calls. No display access. */
void home_wifi_start(home_ui *ui,SemaphoreHandle_t lock);
/* Screen asleep: let the Wi-Fi radio doze between beacons (WIFI_PS_MIN_MODEM); awake: radio always on
 * so voice uploads are not paced by DTIM wake-ups. Safe from any task; no-op before Wi-Fi starts. */
void home_wifi_power_save(bool asleep);
/* Sleep (power_main.c): true = disconnect and stop the radio, false = start it again and rejoin the
 * saved network in the background. Returns at once; the Wi-Fi worker applies it. */
void home_wifi_sleep(bool asleep);
/* The worker has stopped the radio for sleep (light sleep needs Wi-Fi stopped). */
bool home_wifi_is_asleep(void);
