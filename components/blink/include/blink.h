#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blink *blink_handle_t;

typedef enum {
    BLINK_COLOR_RED,
    BLINK_COLOR_GREEN,
    BLINK_COLOR_BLUE,
} blink_color_t;

/* 创建单颗 WS2812（数据顺序由组件 Kconfig 选择），并将其熄灭；成功后由调用方持有句柄。 */
esp_err_t blink_create(int gpio_number, blink_handle_t *out_handle);
/* 在当前任务中蓝光亮、灭各 200 ms；只能从任务调用，同一句柄需串行使用。 */
esp_err_t blink_once(blink_handle_t handle);
/* 指定颜色亮、灭各 200 ms；与 blink_once 一样只能在任务中串行调用。 */
esp_err_t blink_once_color(blink_handle_t handle, blink_color_t color);
/* 先结束所有使用，再释放驱动；销毁前应确保灯已熄灭。 */
esp_err_t blink_delete(blink_handle_t handle);

/* 在指定数据引脚上持续闪烁蓝光，数据顺序由 Kconfig 选择；正常运行时不返回。 */
esp_err_t blink_run(int gpio_number);

#ifdef __cplusplus
}
#endif
