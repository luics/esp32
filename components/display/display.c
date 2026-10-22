#include "display.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "color_font.h"

#define WIDTH 240
#define HEIGHT 320
#define STRIPE_HEIGHT 16
static const char *TAG = "display";
static bool initialized;

struct display {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    QueueHandle_t frames;
    SemaphoreHandle_t transfer_done;
    uint16_t *stripe;
};

static bool on_transfer_done(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_io_event_data_t *event, void *context)
{
    (void)io;
    (void)event;
    struct display *display = context;
    BaseType_t wake = pdFALSE;
    /* DMA 完成后才允许任务改写行缓冲，避免正在发送的像素被覆盖。 */
    xSemaphoreGiveFromISR(display->transfer_done, &wake);
    return wake == pdTRUE;
}

static uint16_t wire_color(uint16_t rgb565)
{
    /* ILI9341 按高字节优先接收 RGB565，ESP32 内存为小端。 */
    return (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
}

static void draw_text(struct display *display, int stripe_top, int left, int text_top,
                       const char *text, int scale)
{
    /* 只绘制与当前 DMA 条带相交的文字；黑字覆盖白底。 */
    for (size_t i = 0; text[i]; ++i) {
        unsigned char character = (unsigned char)text[i];
        if (character < 32 || character > 126) character = '?';
        const int origin_x = left + (int)i * 6 * scale;
        for (int y = 0; y < 9 * scale; ++y) {
            const int row = text_top + y - stripe_top;
            if (row < 0 || row >= STRIPE_HEIGHT) continue;
            for (int x = 0; x < 5 * scale; ++x) {
                const int column = origin_x + x;
                if (column < 0 || column >= WIDTH) continue;
                if (telegraph_font[character - 32][y / scale] & (1U << (4 - x / scale))) {
                    display->stripe[row * WIDTH + column] = 0;
                }
            }
        }
    }
}

static void centered(struct display *display, int top, int y, const char *text, int scale)
{
    draw_text(display, top, (WIDTH - (int)strlen(text) * 6 * scale + scale) / 2, y, text, scale);
}

static void render_stripe(struct display *display, int top, const display_frame_t *frame)
{
    for (int i = 0; i < WIDTH * STRIPE_HEIGHT; ++i) display->stripe[i] = wire_color(0xffff);
    char label[48];
    centered(display, top, 8, "MORSE TELEGRAPH", 2);
    snprintf(label, sizeof(label), "INTERVAL %lu ms / INPUT %lu ms",
             (unsigned long)frame->selected_unit_ms, (unsigned long)frame->character_unit_ms);
    centered(display, top, 33, label, 1);
    centered(display, top, 57, frame->current_code, 3);
    const char decoded[] = {frame->decoded ? frame->decoded : ' ', '\0'};
    centered(display, top, 95, decoded, 5);
    snprintf(label, sizeof(label), "HISTORY %u/%u  CHARACTERS %u", frame->page ? frame->page : 1,
             frame->pages ? frame->pages : 1, frame->characters);
    draw_text(display, top, 9, 169, label, 1);
    /* 历史区域固定在底部，只包含已确认字符；页码随四秒轮播更新。 */
    for (int row = 0; row < 10; ++row) draw_text(display, top, 9, 188 + row * 13, frame->history[row], 1);
}

static esp_err_t draw_frame(struct display *display, const display_frame_t *frame)
{
    for (int y = 0; y < HEIGHT; y += STRIPE_HEIGHT) {
        /* 只取消旧画面刷新，不改变应用保存的完整历史。 */
        if (uxQueueMessagesWaiting(display->frames)) return ESP_OK;
        render_stripe(display, y, frame);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, 0, y, WIDTH,
                                                     y + STRIPE_HEIGHT, display->stripe),
                            TAG, "Failed to draw stripe");
        xSemaphoreTake(display->transfer_done, portMAX_DELAY);
    }
    return ESP_OK;
}

static void display_worker(void *context)
{
    struct display *display = context;
    display_frame_t frame = {0};
    while (true) {
        const esp_err_t ret = draw_frame(display, &frame);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to draw frame: %s", esp_err_to_name(ret));
        xQueueReceive(display->frames, &frame, portMAX_DELAY);
    }
}

esp_err_t display_init(const display_config_t *config, display_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Missing output handle");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Missing display config");
    ESP_RETURN_ON_FALSE(!initialized, ESP_ERR_INVALID_STATE, TAG, "Display already initialized");
    const int pins[] = {config->sclk_gpio, config->mosi_gpio, config->cs_gpio,
                        config->dc_gpio, config->reset_gpio};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(pins[i]), ESP_ERR_INVALID_ARG,
                            TAG, "Invalid display GPIO %d", pins[i]);
        for (size_t j = 0; j < i; ++j) {
            ESP_RETURN_ON_FALSE(pins[i] != pins[j], ESP_ERR_INVALID_ARG, TAG, "Duplicate display GPIO");
        }
    }
    struct display *display = calloc(1, sizeof(*display));
    ESP_RETURN_ON_FALSE(display, ESP_ERR_NO_MEM, TAG, "Failed to allocate display");
    esp_err_t ret = ESP_ERR_NO_MEM;
    bool bus_ready = false;
    display->frames = xQueueCreate(1, sizeof(display_frame_t));
    display->transfer_done = xSemaphoreCreateBinary();
    display->stripe = heap_caps_malloc(WIDTH * STRIPE_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!display->frames || !display->transfer_done || !display->stripe) {
        goto cleanup;
    }
    /* 只分配 16 行 DMA 缓冲，避免整屏 RGB565 帧占用 150 KiB 内存。 */
    const spi_bus_config_t bus = {
        .sclk_io_num = config->sclk_gpio, .mosi_io_num = config->mosi_gpio,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = WIDTH * STRIPE_HEIGHT * sizeof(uint16_t),
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), cleanup, TAG, "SPI init failed");
    bus_ready = true;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = config->cs_gpio, .dc_gpio_num = config->dc_gpio,
        .spi_mode = 0, .pclk_hz = 40 * 1000 * 1000, .trans_queue_depth = 1,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = on_transfer_done, .user_ctx = display,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &display->io), cleanup, TAG, "Panel IO init failed");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->reset_gpio,
        /* 面板使用 BGR 子像素顺序；绘图缓冲仍使用标准 RGB565。 */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, .bits_per_pixel = 16,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_ili9341(display->io, &panel_config, &display->panel), cleanup, TAG, "Panel init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(display->panel), cleanup, TAG, "Panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(display->panel), cleanup, TAG, "Panel commands failed");
    /* 竖屏连接器朝下：设置横向扫描方向，避免文字左右镜像。 */
    ESP_GOTO_ON_ERROR(esp_lcd_panel_mirror(display->panel, true, false), cleanup, TAG, "Panel orientation failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), cleanup, TAG, "Display on failed");
    /* 独立任务显示最新快照；初始化成功后实例与任务随应用持续运行。 */
    if (xTaskCreate(display_worker, "display", 3072, display, 2, NULL) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    initialized = true;
    *out_handle = display;
    ESP_LOGI(TAG, "Ready: ILI9341 240x320, SPI 40 MHz");
    return ESP_OK;

cleanup:
    /* 逆序释放初始化过程中取得的资源，保留最初错误码。 */
    if (display->panel) {
        const esp_err_t err = esp_lcd_panel_del(display->panel);
        if (err != ESP_OK) ESP_LOGW(TAG, "Panel cleanup failed: %s", esp_err_to_name(err));
    }
    if (display->io) {
        const esp_err_t err = esp_lcd_panel_io_del(display->io);
        if (err != ESP_OK) ESP_LOGW(TAG, "Panel IO cleanup failed: %s", esp_err_to_name(err));
    }
    if (bus_ready) {
        const esp_err_t err = spi_bus_free(SPI2_HOST);
        if (err != ESP_OK) ESP_LOGW(TAG, "SPI cleanup failed: %s", esp_err_to_name(err));
    }
    if (display->frames) vQueueDelete(display->frames);
    if (display->transfer_done) vSemaphoreDelete(display->transfer_done);
    free(display->stripe);
    free(display);
    return ret;
}

esp_err_t display_show_frame(display_handle_t handle, const display_frame_t *frame)
{
    ESP_RETURN_ON_FALSE(handle && frame, ESP_ERR_INVALID_ARG, TAG, "Missing display frame");
    xQueueOverwrite(handle->frames, frame);
    return ESP_OK;
}
