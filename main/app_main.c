#include "blink.h"
#include "beep.h"
#include "display.h"
#include "button_input.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

static const char *TAG = "app";

typedef struct {
    int gpio;
    blink_color_t color;
    const char *name;
    display_color_t screen_color;
    display_handle_t screen;
    QueueHandle_t events;
    button_input_handle_t handle;
} color_button_t;

static void on_button_press(void *context)
{
    const color_button_t *button = context;
    /* 声音独立触发，不等待短鸣结束。 */
    const esp_err_t ret = beep_trigger();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to trigger beep: %s", esp_err_to_name(ret));
    }
    /* 屏幕和闪灯分别提交最新颜色，SPI 刷新不会阻塞按钮回调。 */
    const esp_err_t display_ret = display_show_color(button->screen, button->screen_color);
    if (display_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update display: %s", esp_err_to_name(display_ret));
    }
    /* 单槽队列覆盖尚未处理的旧颜色，唤醒主任务立即响应最新按下。 */
    xQueueOverwrite(button->events, &button->color);
}

void app_main(void)
{
    color_button_t buttons[] = {
        {.gpio = CONFIG_BUTTON_INPUT_RED_GPIO, .color = BLINK_COLOR_RED, .name = "red", .screen_color = DISPLAY_COLOR_RED},
        {.gpio = CONFIG_BUTTON_INPUT_GPIO, .color = BLINK_COLOR_GREEN, .name = "green", .screen_color = DISPLAY_COLOR_GREEN},
        {.gpio = CONFIG_BUTTON_INPUT_BLUE_GPIO, .color = BLINK_COLOR_BLUE, .name = "blue", .screen_color = DISPLAY_COLOR_BLUE},
    };
    const size_t button_count = sizeof(buttons) / sizeof(buttons[0]);
    /* 初始化前检查所有引脚，避免按钮、灯珠及蜂鸣器互相覆盖 GPIO。 */
    ESP_ERROR_CHECK(CONFIG_BEEP_GPIO == CONFIG_BLINK_RGB_GPIO
                    ? ESP_ERR_INVALID_ARG : ESP_OK);
    for (size_t i = 0; i < button_count; ++i) {
        ESP_ERROR_CHECK(buttons[i].gpio == CONFIG_BEEP_GPIO
                        ? ESP_ERR_INVALID_ARG : ESP_OK);
        ESP_ERROR_CHECK(buttons[i].gpio == CONFIG_BLINK_RGB_GPIO
                        ? ESP_ERR_INVALID_ARG : ESP_OK);
        for (size_t j = 0; j < i; ++j) {
            ESP_ERROR_CHECK(buttons[i].gpio == buttons[j].gpio
                            ? ESP_ERR_INVALID_ARG : ESP_OK);
        }
    }

    const display_config_t screen_config = {
        .sclk_gpio = CONFIG_DISPLAY_SCLK_GPIO, .mosi_gpio = CONFIG_DISPLAY_MOSI_GPIO,
        .cs_gpio = CONFIG_DISPLAY_CS_GPIO, .dc_gpio = CONFIG_DISPLAY_DC_GPIO,
        .reset_gpio = CONFIG_DISPLAY_RST_GPIO,
    };
    const int screen_pins[] = {screen_config.sclk_gpio, screen_config.mosi_gpio,
                               screen_config.cs_gpio, screen_config.dc_gpio, screen_config.reset_gpio};
    /* 显示接口新增五条线，先验证与所有已有外设没有 GPIO 冲突。 */
    for (size_t i = 0; i < sizeof(screen_pins) / sizeof(screen_pins[0]); ++i) {
        ESP_ERROR_CHECK(screen_pins[i] == CONFIG_BEEP_GPIO || screen_pins[i] == CONFIG_BLINK_RGB_GPIO
                        ? ESP_ERR_INVALID_ARG : ESP_OK);
        for (size_t j = 0; j < button_count; ++j) {
            ESP_ERROR_CHECK(screen_pins[i] == buttons[j].gpio ? ESP_ERR_INVALID_ARG : ESP_OK);
        }
    }
    display_handle_t screen = NULL;
    ESP_ERROR_CHECK(display_init(&screen_config, &screen));

    /* 上电灭灯，单槽队列只保留最新颜色，避免快速点击积压。 */
    blink_handle_t led = NULL;
    ESP_ERROR_CHECK(blink_create(CONFIG_BLINK_RGB_GPIO, &led));
    QueueHandle_t events = xQueueCreate(1, sizeof(blink_color_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);

    /* 先让蜂鸣器准备就绪，再启用按钮回调，确保首次按下也能发声。 */
    ESP_ERROR_CHECK(beep_init(CONFIG_BEEP_GPIO, CONFIG_BEEP_FREQUENCY_HZ,
                              CONFIG_BEEP_DURATION_MS));

    /* 三个按钮共用回调，各自上下文保存颜色；主任务不返回，保证上下文有效。 */
    for (size_t i = 0; i < button_count; ++i) {
        buttons[i].screen = screen;
        buttons[i].events = events;
        ESP_ERROR_CHECK(button_input_create(buttons[i].gpio, on_button_press,
                                            &buttons[i], &buttons[i].handle));
        ESP_LOGI(TAG, "Ready: %s button GPIO %d", buttons[i].name, buttons[i].gpio);
    }
    /* 灭灯时无限等待；亮灯时等待新颜色或 200 ms 到期，不再使用阻塞闪光接口。 */
    TickType_t wait_ticks = portMAX_DELAY;
    while (true) {
        blink_color_t color;
        if (xQueueReceive(events, &color, wait_ticks) == pdTRUE) {
            /* 新事件立即切色；同色重复按下也会从这次显示起重新计时。 */
            ESP_ERROR_CHECK(blink_set_color(led, color));
            wait_ticks = pdMS_TO_TICKS(200);
            ESP_LOGI(TAG, "Button pressed: show %s for 200 ms", buttons[color].name);
        } else {
            /* 期间没有新事件才灭灯；灭灯后仍可立即响应下一次按下。 */
            ESP_ERROR_CHECK(blink_off(led));
            wait_ticks = portMAX_DELAY;
        }
    }
}
