#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* 启动阶段调用一次，LEDC 低速 timer 0/channel 0，初始静音。 */
esp_err_t beep_init(int gpio_number, uint32_t frequency_hz);
/* 从同一任务串行控制；50% 占空比发声，0% 静音，不含延时。 */
esp_err_t beep_set_active(bool active);
