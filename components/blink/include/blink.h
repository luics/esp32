#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在指定数据引脚上持续闪烁单颗 RGB 顺序的 WS2812；正常运行时不返回。 */
esp_err_t blink_run(int gpio_number);

#ifdef __cplusplus
}
#endif
