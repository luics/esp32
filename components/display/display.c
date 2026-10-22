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
#include "esp_lcd_ili9341.h"
#endif
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "color_font.h"

#ifdef CONFIG_DISPLAY_WOKWI_SH1107
#define WIDTH 128
#define HEIGHT 128
#else
#define WIDTH 240
#define HEIGHT 320
#endif
#define STRIPE_HEIGHT 16
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
    uint16_t *stripe;
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    i2c_master_bus_handle_t bus;
    uint8_t pixels[SH1107_BUFFER_SIZE];
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
    /* DMA 完成后才允许任务改写行缓冲，避免正在发送的像素被覆盖。 */
    xSemaphoreGiveFromISR(display->transfer_done, &wake);
    return wake == pdTRUE;
}

#endif

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

static void draw_history_status(struct display *display, int top, int y, int margin,
                                const display_frame_t *frame)
{
    char label[32];
    /* 页码与字符锁定单位固定在屏幕底部，两端对齐，为历史内容留出空间。 */
    snprintf(label, sizeof(label), "%u/%u", frame->page ? frame->page : 1,
             frame->pages ? frame->pages : 1);
    draw_text(display, top, margin, y, label, 1);
    snprintf(label, sizeof(label), "%c %luMS", frame->dual_mode ? 'D' : 'S', (unsigned long)frame->character_unit_ms);
    draw_text(display, top, WIDTH - margin - (int)strlen(label) * 6 + 1, y, label, 1);
}

static void render_stripe(struct display *display, int top, const display_frame_t *frame)
{
    for (int i = 0; i < WIDTH * STRIPE_HEIGHT; ++i) display->stripe[i] = wire_color(0xffff);
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    /* 128×128 保留输入与历史两区；八个点划均可在一行完整展示。 */
    centered(display, top, 2, "MORSE TELEGRAPH", 1);
    centered(display, top, 16, frame->current_code, 2);
    const char decoded[] = {frame->decoded ? frame->decoded : ' ', '\0'};
    centered(display, top, 35, decoded, 2);
    draw_history_status(display, top, HEIGHT - 11, 4, frame);
    for (int row = 0; row < DISPLAY_HISTORY_ROWS; ++row) {
        draw_text(display, top, 4, 55 + row * 10, frame->history[row], 1);
    }
#else
    centered(display, top, 8, "MORSE TELEGRAPH", 2);
    centered(display, top, 33, frame->current_code, 3);
    const char decoded[] = {frame->decoded ? frame->decoded : ' ', '\0'};
    centered(display, top, 71, decoded, 5);
    draw_history_status(display, top, HEIGHT - 11, 9, frame);
    /* 历史区域固定在底部，只包含已确认字符；页码由绿键手动切换。 */
    for (int row = 0; row < 10; ++row) draw_text(display, top, 9, 145 + row * 13, frame->history[row], 1);
#endif
    /* 右上角小黑点表示可以开始新词，状态完全由输入任务的词间隔决定。 */
    for (int y = 0; y < WORD_DOT_SIZE; ++y) {
        const int row = WORD_DOT_TOP + y - top;
        if (row < 0 || row >= STRIPE_HEIGHT) continue;
        for (int x = 0; x < WORD_DOT_SIZE; ++x) {
            const int dx = x - WORD_DOT_SIZE / 2;
            const int dy = y - WORD_DOT_SIZE / 2;
            if (frame->word_ready && dx * dx + dy * dy <= WORD_DOT_RADIUS * WORD_DOT_RADIUS) {
                display->stripe[row * WIDTH + WORD_DOT_LEFT + x] = 0;
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
                sh1107_set_pixel(display->pixels, x, y + row, display->stripe[row * WIDTH + x] != 0);
            }
        }
#else
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, 0, y, WIDTH,
                                                     y + STRIPE_HEIGHT, display->stripe),
                            TAG, "Failed to draw stripe");
        xSemaphoreTake(display->transfer_done, portMAX_DELAY);
#endif
    }
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, 0, 0, WIDTH, HEIGHT,
                                                 display->pixels), TAG, "Failed to draw OLED frame");
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
            display->stripe[y * WORD_DOT_SIZE + x] = black ? 0 : wire_color(0xffff);
#endif
        }
    }
#ifdef CONFIG_DISPLAY_WOKWI_SH1107
    /* 仅刷新覆盖指示点的前两页；保留同页标题和状态文字。 */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, 0, 0, WIDTH, 16,
                                                 display->pixels), TAG, "Failed to draw OLED word indicator");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, WORD_DOT_LEFT, WORD_DOT_TOP,
                                                 WORD_DOT_LEFT + WORD_DOT_SIZE, WORD_DOT_TOP + WORD_DOT_SIZE,
                                                 display->stripe), TAG, "Failed to draw word indicator");
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
    display->stripe = heap_caps_malloc(WIDTH * STRIPE_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
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
    ESP_LOGI(TAG, "Ready: ILI9341 240x320, SPI 40 MHz");
#endif
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
