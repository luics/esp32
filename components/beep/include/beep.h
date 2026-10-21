#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化单个无源蜂鸣器，默认静音；占用 LEDC 低速 timer 0/channel 0。
 * 仅在启动阶段调用一次；成功后组件随应用持续运行。
 * 频率范围 100~10000 Hz，时长范围 1~60000 ms。 */
esp_err_t beep_init(int gpio_number, uint32_t frequency_hz, uint32_t duration_ms);
/* 从任务或按钮回调触发短鸣，不等待播放；连续触发重新计时，不适用于 ISR。 */
esp_err_t beep_trigger(void);

#ifdef __cplusplus
}
#endif
