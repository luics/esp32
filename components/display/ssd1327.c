#include "ssd1327.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ssd1327";

esp_err_t ssd1327_set_on(esp_lcd_panel_io_handle_t io, bool on)
{
    return esp_lcd_panel_io_tx_param(io, on ? 0xaf : 0xae, NULL, 0);
}

esp_err_t ssd1327_init(esp_lcd_panel_io_handle_t io, int reset_gpio)
{
    ESP_RETURN_ON_FALSE(io && GPIO_IS_VALID_OUTPUT_GPIO(reset_gpio), ESP_ERR_INVALID_ARG,
                        TAG, "Invalid OLED IO or reset GPIO");
    /* 按微雪 1.5inch OLED 时序复位，初始化期间保持关屏，避免显示随机 RAM。 */
    const gpio_config_t reset = {.pin_bit_mask = 1ULL << reset_gpio, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "Reset GPIO init failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), TAG, "Reset high failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 0), TAG, "Reset low failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), TAG, "Reset release failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(ssd1327_set_on(io, false), TAG, "Display off failed");
    /* 寄存器值依据微雪官方 OLED_1in5 初始化序列；0x51 为水平寻址/面板重映射。 */
    static const uint8_t settings[][2] = {
        {0xfd, 0x12}, {0x81, 0x80}, {0xa0, 0x51}, {0xa1, 0x00}, {0xa2, 0x00},
        {0xa8, 0x7f}, {0xb1, 0xf1}, {0xb3, 0x00}, {0xab, 0x01}, {0xb6, 0x0f},
        {0xbe, 0x0f}, {0xbc, 0x08}, {0xd5, 0x62},
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, settings[i][0], &settings[i][1], 1),
                            TAG, "OLED configuration failed");
    }
    /* 正常 RAM 显示及默认灰度表；参数与命令均以 DC=0 发送。 */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xa4, NULL, 0), TAG, "Normal mode failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xb9, NULL, 0), TAG, "Gray table failed");
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

esp_err_t ssd1327_draw_rows(esp_lcd_panel_io_handle_t io, int top, int rows,
                           const uint8_t *pixels, size_t size)
{
    ESP_RETURN_ON_FALSE(io && pixels && top >= 0 && top < SSD1327_HEIGHT && rows > 0 &&
                        rows <= SSD1327_HEIGHT - top && size == (size_t)rows * SSD1327_ROW_BYTES,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid OLED row window or buffer");
    const uint8_t columns[] = {0, SSD1327_ROW_BYTES - 1};
    const uint8_t lines[] = {(uint8_t)top, (uint8_t)(top + rows - 1)};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0x15, columns, sizeof(columns)), TAG, "Column window failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0x75, lines, sizeof(lines)), TAG, "Row window failed");
    /* SSD1327 设置窗口后直接写显存，不发送其他控制器的 RAMWR 命令。 */
    return esp_lcd_panel_io_tx_color(io, -1, pixels, size);
}
