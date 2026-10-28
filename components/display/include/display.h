#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#define DISPLAY_HISTORY_ROWS 6
#define DISPLAY_HISTORY_COLS 20

typedef struct display *display_handle_t;
typedef struct {
    /* SPI SCK/MOSI；SH1107 仿真中分别作为 I2C SCL/SDA。 */
    int sclk_gpio;
    int mosi_gpio;
    int cs_gpio;
    int dc_gpio;
    int reset_gpio;
} display_config_t;

/* 白底、黑色输入与状态、灰色历史；只传递显示快照，输入逻辑及历史由应用管理。 */
typedef struct {
    uint32_t selected_unit_ms;
    uint32_t character_unit_ms;
    /* 新词可开始：初始显示，按住及词内静默时隐藏。 */
    bool word_ready;
    bool dual_mode;
    char current_code[10];
    char decoded;
    bool dp_preview_active;
    char dp_preview[2][21]; /* 回答的最新两行，每行最多 20 个 ASCII 字符。 */
    unsigned page;
    unsigned pages;
    char label[4]; /* 本次开机标签：三个大写字母或数字，以 NUL 结尾。 */
    unsigned characters;
    char history[10][38];
} display_frame_t;

esp_err_t display_init(const display_config_t *config, display_handle_t *out_handle);
/* 单槽队列保存最新完整画面，立即返回，不丢失应用历史。 */
esp_err_t display_show_frame(display_handle_t handle, const display_frame_t *frame);
