#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* 输入任务串行控制 LED；快照和开关命令通过单槽队列传递。 */
esp_err_t telegraph_ha_start(void);
void telegraph_ha_update(bool on);
bool telegraph_ha_take_led(bool *on);
