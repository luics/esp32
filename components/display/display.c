#include "display.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
#include "driver/i2c_master.h"
#include "esp_lcd_sh1107.h"
#include "sh1107_pixels.h"
#else
#include "ssd1327.h"
#include "ssd1327_pixels.h"
#endif
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "color_font.h"

#define WIDTH 128
#define HEIGHT 128
#define STRIPE_HEIGHT 16
#define GRAY_BLACK 0
#define GRAY_HISTORY 6
#define GRAY_WHITE 15
#define WORD_DOT_LEFT (WIDTH - 12)
#define WORD_DOT_TOP 4
#define WORD_DOT_SIZE 7
#define WORD_DOT_RADIUS 2
static const char *TAG = "display";
static bool initialized;

struct display {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    QueueHandle_t frames;
    SemaphoreHandle_t transfer_done;
    uint8_t *stripe;
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    i2c_master_bus_handle_t bus;
    uint8_t pixels[SH1107_BUFFER_SIZE];
#else
    uint8_t *pixels;
#endif
};

#ifndef CONFIG_DISPLAY_WOKWI_SH1107
static bool on_transfer_done(esp_lcd_panel_io_handle_t io,
                             esp_lcd_panel_io_event_data_t *event, void *context)
{
    (void)io;
    (void)event;
    struct display *display = context;
    BaseType_t wake = pdFALSE;
    /* DMA 完成后才允许任务改写灰度帧缓冲，避免正在发送的像素被覆盖。 */
    xSemaphoreGiveFromISR(display->transfer_done, &wake);
    return wake == pdTRUE;
}

#endif

static void draw_text(struct display *display, int stripe_top, int left, int text_top,
                       const char *text, int scale, uint8_t gray)
{
    /* 只绘制与当前渲染条带相交的文字；指定灰度覆盖白底。 */
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
                    display->stripe[row * WIDTH + column] = gray;
                }
            }
        }
    }
}

static void centered(struct display *display, int top, int y, const char *text, int scale)
{
    draw_text(display, top, (WIDTH - (int)strlen(text) * 6 * scale + scale) / 2, y, text, scale, GRAY_BLACK);
}

static void draw_history_status(struct display *display, int top, int y, int margin,
                                const display_frame_t *frame)
{
    char label[32];
    /* 页码右侧显示开机标签，字符锁定单位仍在底栏右端。 */
    snprintf(label, sizeof(label), "%u/%u%s%.3s", frame->page ? frame->page : 1,
             frame->pages ? frame->pages : 1, frame->label[0] ? " " : "",
             frame->label);
    draw_text(display, top, margin, y, label, 1, GRAY_BLACK);
    snprintf(label, sizeof(label), "%c %luMS", frame->dual_mode ? 'D' : 'S', (unsigned long)frame->character_unit_ms);
    draw_text(display, top, WIDTH - margin - (int)strlen(label) * 6 + 1, y, label, 1, GRAY_BLACK);
}

static void draw_title(struct display *display, int top)
{
    static const char title[] = "MORSE TELEGRAPH";
    const int glyph_height = 11;
    const int pitch = 7;
    const int left = (WIDTH - ((int)strlen(title) * pitch - 1)) / 2;
    /* 标题从 9 像素字模增高到 11 像素，并向右扩展 1 像素加粗；保留新词指示空间。 */
    for (size_t i = 0; title[i]; ++i) {
        for (int y = 0; y < glyph_height; ++y) {
            const int row = 2 + y - top;
            if (row < 0 || row >= STRIPE_HEIGHT) continue;
            const uint8_t bits = telegraph_font[(unsigned char)title[i] - 32][y * 9 / glyph_height];
            for (int x = 0; x < 5; ++x) {
                if (!(bits & (1U << (4 - x)))) continue;
                const int column = left + (int)i * pitch + x;
                display->stripe[row * WIDTH + column] = GRAY_BLACK;
                display->stripe[row * WIDTH + column + 1] = GRAY_BLACK;
            }
        }
    }
}

static void render_stripe(struct display *display, int top, const display_frame_t *frame)
{
    memset(display->stripe, GRAY_WHITE, WIDTH * STRIPE_HEIGHT);
    /* 128×128 保留输入与历史两区；八个点划均可在一行完整展示。 */
    draw_title(display, top);
    centered(display, top, 16, frame->current_code, 2);
    const char decoded[] = {frame->decoded ? frame->decoded : ' ', '\0'};
    centered(display, top, 35, decoded, 2);
    draw_history_status(display, top, HEIGHT - 11, 4, frame);
    for (int row = 0; row < DISPLAY_HISTORY_ROWS; ++row) {
        draw_text(display, top, 4, 55 + row * 10, frame->history[row], 1, GRAY_HISTORY);
    }
    /* 右上角小黑点表示可以开始新词，状态完全由输入任务的词间隔决定。 */
    for (int y = 0; y < WORD_DOT_SIZE; ++y) {
        const int row = WORD_DOT_TOP + y - top;
        if (row < 0 || row >= STRIPE_HEIGHT) continue;
        for (int x = 0; x < WORD_DOT_SIZE; ++x) {
            const int dx = x - WORD_DOT_SIZE / 2;
            const int dy = y - WORD_DOT_SIZE / 2;
            if (frame->word_ready && dx * dx + dy * dy <= WORD_DOT_RADIUS * WORD_DOT_RADIUS) {
                display->stripe[row * WIDTH + WORD_DOT_LEFT + x] = GRAY_BLACK;
            }
        }
    }
}

static esp_err_t draw_frame(struct display *display, const display_frame_t *frame)
{
    for (int y = 0; y < HEIGHT; y += STRIPE_HEIGHT) {
        /* 只取消旧画面刷新，不改变应用保存的完整历史。 */
        if (uxQueueMessagesWaiting(display->frames)) return ESP_OK;
        render_stripe(display, y, frame);
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
        /* 共用文字渲染，再转换为 OLED 的纵向 8 像素页；I2C 同步完成后才能复用。 */
        for (int row = 0; row < STRIPE_HEIGHT; ++row) {
            for (int x = 0; x < WIDTH; ++x) {
                sh1107_set_pixel(display->pixels, x, y + row, display->stripe[row * WIDTH + x] == GRAY_WHITE);
            }
        }
#else
        /* 灰度显存按行打包；完整渲染后才交给 DMA，取消旧画面不会刷出半帧。 */
        for (int row = 0; row < STRIPE_HEIGHT; ++row) {
            for (int x = 0; x < WIDTH; ++x) {
                ssd1327_set_pixel(display->pixels, WIDTH, x, y + row,
                                   display->stripe[row * WIDTH + x]);
            }
        }
#endif
    }
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, 0, 0, WIDTH, HEIGHT,
                                                 display->pixels), TAG, "Failed to draw OLED frame");
#else
    ESP_RETURN_ON_ERROR(ssd1327_draw_rows(display->io, 0, HEIGHT, display->pixels,
                                          SSD1327_BUFFER_SIZE), TAG, "Failed to draw OLED frame");
    xSemaphoreTake(display->transfer_done, portMAX_DELAY);
#endif
    return ESP_OK;
}

static esp_err_t draw_word_indicator(struct display *display, bool visible)
{
    /* 显隐变化先刷新 7×7 指示区域，随后再更新正文，尽早响应红键。 */
    for (int y = 0; y < WORD_DOT_SIZE; ++y) {
        for (int x = 0; x < WORD_DOT_SIZE; ++x) {
            const int dx = x - WORD_DOT_SIZE / 2;
            const int dy = y - WORD_DOT_SIZE / 2;
            const bool black = visible && dx * dx + dy * dy <= WORD_DOT_RADIUS * WORD_DOT_RADIUS;
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
            sh1107_set_pixel(display->pixels, WORD_DOT_LEFT + x, WORD_DOT_TOP + y, !black);
#else
            ssd1327_set_pixel(display->pixels, WIDTH, WORD_DOT_LEFT + x, WORD_DOT_TOP + y, black ? 0 : 15);
#endif
        }
    }
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    /* 仅刷新覆盖指示点的前两页；保留同页标题和状态文字。 */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, 0, 0, WIDTH, 16,
                                                 display->pixels), TAG, "Failed to draw OLED word indicator");
#else
    /* 按完整行刷新前 16 行，保留指示点两侧及同区标题，列地址始终以字节对齐。 */
    ESP_RETURN_ON_ERROR(ssd1327_draw_rows(display->io, 0, 16, display->pixels,
                                          SSD1327_ROW_BYTES * 16), TAG, "Failed to draw word indicator");
    xSemaphoreTake(display->transfer_done, portMAX_DELAY);
#endif
    return ESP_OK;
}

static void display_worker(void *context)
{
    struct display *display = context;
    display_frame_t frame = {
        .selected_unit_ms = 100, .character_unit_ms = 100, .word_ready = true, .dual_mode = true,
    };
    bool visible = true;
    while (true) {
        /* 新词指示随快照更新，不再由显示任务独立计时或周期闪烁。 */
        if (frame.word_ready != visible) {
            const esp_err_t ret = draw_word_indicator(display, frame.word_ready);
            if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to draw word indicator: %s", esp_err_to_name(ret));
            visible = frame.word_ready;
        }
        const esp_err_t ret = draw_frame(display, &frame);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to draw display: %s", esp_err_to_name(ret));
        xQueueReceive(display->frames, &frame, portMAX_DELAY);
    }
}

esp_err_t display_init(const display_config_t *config, display_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Missing output handle");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Missing display config");
    ESP_RETURN_ON_FALSE(!initialized, ESP_ERR_INVALID_STATE, TAG, "Display already initialized");
    const int pins[] = {config->sclk_gpio, config->mosi_gpio,
#ifndef CONFIG_DISPLAY_WOKWI_SH1107
                        config->cs_gpio, config->dc_gpio, config->reset_gpio,
#endif
    };
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
#ifndef CONFIG_DISPLAY_WOKWI_SH1107
    bool bus_ready = false;
#endif
    display->frames = xQueueCreate(1, sizeof(display_frame_t));
    display->transfer_done = xSemaphoreCreateBinary();
    display->stripe = malloc(WIDTH * STRIPE_HEIGHT);
#ifndef CONFIG_DISPLAY_WOKWI_SH1107
    display->pixels = heap_caps_malloc(SSD1327_BUFFER_SIZE, MALLOC_CAP_DMA);
    if (!display->pixels) goto cleanup;
    memset(display->pixels, 0xff, SSD1327_BUFFER_SIZE);
#endif
    if (!display->frames || !display->transfer_done || !display->stripe) {
        goto cleanup;
    }
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    /* 仿真与实板总线分别初始化，CS/DC/RST 不占用仿真 GPIO。 */
    const i2c_master_bus_config_t bus = {
        .i2c_port = -1, .scl_io_num = config->sclk_gpio, .sda_io_num = config->mosi_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_GOTO_ON_ERROR(i2c_new_master_bus(&bus, &display->bus), cleanup, TAG, "I2C init failed");
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_IO_I2C_SH1107_CONFIG();
    io_config.scl_speed_hz = 400000;
    io_config.transaction_timeout_ms = 100;
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_i2c(display->bus, &io_config, &display->io), cleanup, TAG, "OLED IO init failed");
    const esp_lcd_panel_sh1107_config_t vendor_config = {.contrast = 128, .offset = 0};
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, .bits_per_pixel = 1, .vendor_config = (void *)&vendor_config,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_sh1107(display->io, &panel_config, &display->panel), cleanup, TAG, "OLED init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(display->panel), cleanup, TAG, "OLED reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(display->panel), cleanup, TAG, "OLED commands failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), cleanup, TAG, "OLED on failed");
#else
    /* 实板使用 SPI2 和 8192 字节灰度 DMA 帧缓冲，SPI 先以 4 MHz 工作。 */
    const spi_bus_config_t bus = {
        .sclk_io_num = config->sclk_gpio, .mosi_io_num = config->mosi_gpio,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = SSD1327_BUFFER_SIZE,
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), cleanup, TAG, "SPI init failed");
    bus_ready = true;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = config->cs_gpio, .dc_gpio_num = config->dc_gpio,
        .spi_mode = 0, .pclk_hz = 4 * 1000 * 1000, .trans_queue_depth = 1,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .flags.dc_low_on_param = true,
        .on_color_trans_done = on_transfer_done, .user_ctx = display,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &display->io), cleanup, TAG, "Panel IO init failed");
    ESP_GOTO_ON_ERROR(ssd1327_init(display->io, config->reset_gpio), cleanup, TAG, "SSD1327 init failed");
    /* 关屏时先写入白底，DMA 完成后开屏，避免上电随机像素。 */
    ESP_GOTO_ON_ERROR(ssd1327_draw_rows(display->io, 0, HEIGHT, display->pixels,
                                        SSD1327_BUFFER_SIZE), cleanup, TAG, "OLED clear failed");
    xSemaphoreTake(display->transfer_done, portMAX_DELAY);
    ESP_GOTO_ON_ERROR(ssd1327_set_on(display->io, true), cleanup, TAG, "OLED on failed");
#endif
    /* 独立任务显示最新快照；初始化成功后实例与任务随应用持续运行。 */
    if (xTaskCreate(display_worker, "display", 3072, display, 2, NULL) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    initialized = true;
    *out_handle = display;
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    ESP_LOGI(TAG, "Ready: SH1107 128x128 monochrome, I2C 400 kHz (SSD1327 approximation)");
#else
    ESP_LOGI(TAG, "Ready: SSD1327 128x128, 4-bit grayscale, SPI 4 MHz");
#endif
    return ESP_OK;

cleanup:
    /* 逆序释放初始化过程中取得的资源，保留最初错误码。 */
    if (display->panel) {
        const esp_err_t err = esp_lcd_panel_del(display->panel);
        if (err != ESP_OK) ESP_LOGW(TAG, "Panel cleanup failed: %s", esp_err_to_name(err));
    }
#ifndef CONFIG_DISPLAY_WOKWI_SH1107
    if (display->io) {
        const esp_err_t err = ssd1327_set_on(display->io, false);
        if (err != ESP_OK) ESP_LOGW(TAG, "OLED off during cleanup failed: %s", esp_err_to_name(err));
    }
#endif
    if (display->io) {
        const esp_err_t err = esp_lcd_panel_io_del(display->io);
        if (err != ESP_OK) ESP_LOGW(TAG, "Panel IO cleanup failed: %s", esp_err_to_name(err));
    }
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    if (display->bus) {
        const esp_err_t err = i2c_del_master_bus(display->bus);
        if (err != ESP_OK) ESP_LOGW(TAG, "I2C cleanup failed: %s", esp_err_to_name(err));
    }
#else
    if (bus_ready) {
        const esp_err_t err = spi_bus_free(SPI2_HOST);
        if (err != ESP_OK) ESP_LOGW(TAG, "SPI cleanup failed: %s", esp_err_to_name(err));
    }
#endif
    if (display->frames) vQueueDelete(display->frames);
    if (display->transfer_done) vSemaphoreDelete(display->transfer_done);
#ifndef CONFIG_DISPLAY_WOKWI_SH1107
    free(display->pixels);
    const esp_err_t reset_error = gpio_reset_pin(config->reset_gpio);
    if (reset_error != ESP_OK) ESP_LOGW(TAG, "Reset GPIO cleanup failed: %s", esp_err_to_name(reset_error));
#endif
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
