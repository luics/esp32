#include "display.h"
#include <stdlib.h>
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
#define GLYPH_SIZE 32
#define TEXT_GAP 8
#define TEXT_WIDTH (GLYPH_SIZE * 2 + TEXT_GAP)

static const char *TAG = "display";
static bool initialized;

struct display {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    QueueHandle_t colors;
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

static void render_stripe(struct display *display, int top, int color)
{
    const uint16_t backgrounds[] = {0xf800, 0x07e0, 0x001f};
    const uint16_t background = color < 0 ? 0 : backgrounds[color];
    const uint16_t foreground = color == DISPLAY_COLOR_GREEN ? 0 : 0xffff;
    const int text_left = (WIDTH - TEXT_WIDTH) / 2;
    const int text_top = (HEIGHT - GLYPH_SIZE) / 2;
    /* 背景和中文字在同一帧中绘制；文字块在水平、垂直方向均居中。 */
    for (int y = 0; y < STRIPE_HEIGHT; ++y) {
        const int glyph_y = top + y - text_top;
        for (int x = 0; x < WIDTH; ++x) {
            uint16_t pixel = background;
            const int local_x = x - text_left;
            if (color >= 0 && glyph_y >= 0 && glyph_y < GLYPH_SIZE && local_x >= 0) {
                int glyph = -1;
                int glyph_x = local_x;
                if (local_x < GLYPH_SIZE) {
                    glyph = color;
                } else if (local_x >= GLYPH_SIZE + TEXT_GAP && local_x < TEXT_WIDTH) {
                    glyph = 3; /* 第二个字固定为“色”。 */
                    glyph_x -= GLYPH_SIZE + TEXT_GAP;
                }
                if (glyph >= 0 && (color_font[glyph][glyph_y] & (UINT32_C(1) << (31 - glyph_x)))) {
                    pixel = foreground;
                }
            }
            display->stripe[y * WIDTH + x] = wire_color(pixel);
        }
    }
}

static esp_err_t draw_frame(struct display *display, int color)
{
    for (int y = 0; y < HEIGHT; y += STRIPE_HEIGHT) {
        display_color_t pending;
        /* 每完成一条带就检查新颜色，快速点击最多等待一条带的传输。 */
        if (xQueuePeek(display->colors, &pending, 0) == pdTRUE) {
            return ESP_OK;
        }
        render_stripe(display, y, color);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(display->panel, 0, y, WIDTH,
                                                     y + STRIPE_HEIGHT, display->stripe),
                            TAG, "Failed to draw stripe");
        /* 缓冲区在 DMA 完成前保持有效，等待只影响显示任务。 */
        xSemaphoreTake(display->transfer_done, portMAX_DELAY);
    }
    return ESP_OK;
}

static void display_worker(void *context)
{
    struct display *display = context;
    esp_err_t ret = draw_frame(display, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear screen: %s", esp_err_to_name(ret));
    }
    display_color_t color;
    while (true) {
        xQueueReceive(display->colors, &color, portMAX_DELAY);
        ret = draw_frame(display, color);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to show color: %s", esp_err_to_name(ret));
        }
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
    display->colors = xQueueCreate(1, sizeof(display_color_t));
    display->transfer_done = xSemaphoreCreateBinary();
    display->stripe = heap_caps_malloc(WIDTH * STRIPE_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!display->colors || !display->transfer_done || !display->stripe) {
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
    /* 独立任务按最新颜色刷新；初始化成功后实例与任务随应用持续运行。 */
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
    if (display->colors) vQueueDelete(display->colors);
    if (display->transfer_done) vSemaphoreDelete(display->transfer_done);
    free(display->stripe);
    free(display);
    return ret;
}

esp_err_t display_show_color(display_handle_t handle, display_color_t color)
{
    ESP_RETURN_ON_FALSE(handle && color >= DISPLAY_COLOR_RED && color <= DISPLAY_COLOR_BLUE,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid display color");
    /* 单槽覆盖保持最新事件，调用方不等待 SPI 刷新。 */
    xQueueOverwrite(handle->colors, &color);
    return ESP_OK;
}
