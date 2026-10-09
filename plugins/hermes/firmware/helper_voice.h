#pragma once
#include <stddef.h>
#include "home_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
void helper_voice_start(home_ui *state, SemaphoreHandle_t mutex);
