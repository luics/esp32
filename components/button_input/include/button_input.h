#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct button_input *button_input_handle_t;
typedef void (*button_input_callback_t)(void *context);
typedef void (*button_input_edge_callback_t)(bool pressed, void *context);

/* 低电平按下、内部上拉、软件消抖；每次按下回调一次，长按不重复。
 * 回调运行在驱动任务中，必须快速返回，不能阻塞；context 在组件使用期间须有效。
 * 请在初始化任务调用，运行中保留句柄。 */
esp_err_t button_input_create(int gpio_number, button_input_callback_t callback,
                              void *context, button_input_handle_t *out_handle);

/* 按下/松开各回调一次；复用同一 GPIO 驱动和消抖，供应用测量按住时长。 */
esp_err_t button_input_create_edges(int gpio_number, button_input_edge_callback_t callback,
                                    void *context, button_input_handle_t *out_handle);

#ifdef __cplusplus
}
#endif
