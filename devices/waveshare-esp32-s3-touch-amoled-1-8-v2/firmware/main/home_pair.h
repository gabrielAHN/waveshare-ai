#pragma once
#include "home_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
/* Pairing worker API. home_pair_boot/home_pair_service run ONLY on the home_live task, so pairing
 * TLS never overlaps with live-feed TLS. home_pair_load is safe from any task (NVS reads). */
void home_pair_boot(home_ui *ui, SemaphoreHandle_t lock);
/* Handles pending Connect-page requests (scan / enroll / cancel / forget). Returns true when it
 * did network work this cycle (the caller skips its live poll). */
bool home_pair_service(bool connected);
/* Enrolled bridge + device token for the live feed and voice. false = not enrolled. */
bool home_pair_load(pair_record *record, unsigned char token[32]);
void home_pair_live_status(int http);
void home_pair_live_forget(void);  /* no link to poll over: the next answer starts fresh (loading) */
