#pragma once
#include <stdint.h>
#include "esp_err.h"

/* GPIO4 电位器四档：200/150/100/60 ms，默认 200 ms；后台采样带迟滞。 */
esp_err_t interval_control_start(int gpio_number, uint32_t sample_interval_ms);
uint32_t interval_control_get_unit_ms(void);
