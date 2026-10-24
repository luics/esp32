#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#define SSD1327_WIDTH 128
#define SSD1327_HEIGHT 128
#define SSD1327_ROW_BYTES (SSD1327_WIDTH / 2)
#define SSD1327_BUFFER_SIZE (SSD1327_ROW_BYTES * SSD1327_HEIGHT)

/* SPI IO 必须配置 dc_low_on_param；初始化后保持关屏，先清 RAM 再开屏。 */
esp_err_t ssd1327_init(esp_lcd_panel_io_handle_t io, int reset_gpio);
esp_err_t ssd1327_set_on(esp_lcd_panel_io_handle_t io, bool on);
/* 发送完整宽度的连续行；DMA 完成前调用方必须保持 pixels 有效且不改写。 */
esp_err_t ssd1327_draw_rows(esp_lcd_panel_io_handle_t io, int top, int rows,
                           const uint8_t *pixels, size_t size);
