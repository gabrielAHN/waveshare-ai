#pragma once
#include "home_ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
void home_live_start(home_ui*state,SemaphoreHandle_t mutex);
