#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct display *display_handle_t;
typedef struct {
    int sclk_gpio;
    int mosi_gpio;
    int cs_gpio;
    int dc_gpio;
    int reset_gpio;
} display_config_t;

/* 白底黑字；只传递显示快照，输入逻辑及历史由应用管理。 */
typedef struct {
    uint32_t selected_unit_ms;
    uint32_t character_unit_ms;
    char current_code[10];
    char decoded;
    unsigned page;
    unsigned pages;
    unsigned characters;
    char history[10][38];
} display_frame_t;

esp_err_t display_init(const display_config_t *config, display_handle_t *out_handle);
/* 单槽队列保存最新完整画面，立即返回，不丢失应用历史。 */
esp_err_t display_show_frame(display_handle_t handle, const display_frame_t *frame);
