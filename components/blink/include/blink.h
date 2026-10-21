#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct blink *blink_handle_t;

/* 创建单颗 RGB 顺序 WS2812，并将其熄灭；成功后由调用方持有句柄。 */
esp_err_t blink_create(int gpio_number, blink_handle_t *out_handle);
/* 在当前任务中蓝光亮、灭各 500 ms；只能从任务调用，同一句柄需串行使用。 */
esp_err_t blink_once(blink_handle_t handle);
/* 先结束所有使用，再释放驱动；销毁前应确保灯已熄灭。 */
esp_err_t blink_delete(blink_handle_t handle);

/* 在指定数据引脚上持续闪烁单颗 RGB 顺序的 WS2812；正常运行时不返回。 */
esp_err_t blink_run(int gpio_number);

#ifdef __cplusplus
}
#endif
