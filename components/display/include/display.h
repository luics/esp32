#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct display *display_handle_t;
typedef enum {
    DISPLAY_COLOR_RED,
    DISPLAY_COLOR_GREEN,
    DISPLAY_COLOR_BLUE,
} display_color_t;

typedef struct {
    int sclk_gpio;
    int mosi_gpio;
    int cs_gpio;
    int dc_gpio;
    int reset_gpio;
} display_config_t;

/* 创建 240x320 ILI9341 显示实例，独占 SPI2；成功后随应用持续运行。
 * 只在启动时调用一次，屏幕初始为黑色。 */
esp_err_t display_init(const display_config_t *config, display_handle_t *out_handle);
/* 从任务/按钮回调提交最新颜色，立即返回；后台刷新并保留画面，不可从 ISR 调用。 */
esp_err_t display_show_color(display_handle_t handle, display_color_t color);

#ifdef __cplusplus
}
#endif
