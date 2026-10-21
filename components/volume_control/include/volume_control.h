#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 读取 ADC 电位器并持续更新 beep 音量；采样间隔为 10~1000 ms，启动后由后台任务独立采样。 */
esp_err_t volume_control_start(int gpio_number, uint32_t sample_interval_ms);

#ifdef __cplusplus
}
#endif
