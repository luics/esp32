#include "blink.h"
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
    QueueHandle_t events;
    button_input_handle_t handle;
} color_button_t;

static void on_button_press(void *context)
{
    const color_button_t *button = context;
    /* 回调仅将颜色入队，零等待返回；闪光期间仍可检测其他按钮。 */
    if (xQueueSend(button->events, &button->color, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Flash queue full: dropped %s press", button->name);
    }
}

void app_main(void)
{
    color_button_t buttons[] = {
        {.gpio = CONFIG_BUTTON_INPUT_RED_GPIO, .color = BLINK_COLOR_RED, .name = "red"},
        {.gpio = CONFIG_BUTTON_INPUT_GPIO, .color = BLINK_COLOR_GREEN, .name = "green"},
        {.gpio = CONFIG_BUTTON_INPUT_BLUE_GPIO, .color = BLINK_COLOR_BLUE, .name = "blue"},
    };
    const size_t button_count = sizeof(buttons) / sizeof(buttons[0]);
    /* 初始化前检查所有引脚，避免按钮之间或按钮与灯珠之间发生冲突。 */
    for (size_t i = 0; i < button_count; ++i) {
        ESP_ERROR_CHECK(buttons[i].gpio == CONFIG_BLINK_RGB_GPIO
                        ? ESP_ERR_INVALID_ARG : ESP_OK);
        for (size_t j = 0; j < i; ++j) {
            ESP_ERROR_CHECK(buttons[i].gpio == buttons[j].gpio
                            ? ESP_ERR_INVALID_ARG : ESP_OK);
        }
    }

    /* 先初始化灯珠并熄灭，再创建最多缓存 16 次按下的颜色队列。 */
    blink_handle_t led = NULL;
    ESP_ERROR_CHECK(blink_create(CONFIG_BLINK_RGB_GPIO, &led));
    QueueHandle_t events = xQueueCreate(16, sizeof(blink_color_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);

    /* 三个按钮共用回调，各自上下文保存颜色；主任务不返回，保证上下文有效。 */
    for (size_t i = 0; i < button_count; ++i) {
        buttons[i].events = events;
        ESP_ERROR_CHECK(button_input_create(buttons[i].gpio, on_button_press,
                                            &buttons[i], &buttons[i].handle));
        ESP_LOGI(TAG, "Ready: %s button GPIO %d", buttons[i].name, buttons[i].gpio);
    }
    while (true) {
        blink_color_t color;
        /* 按入队顺序逐次闪光，保留连续点击的颜色及顺序；长按不重复入队。 */
        if (xQueueReceive(events, &color, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Button pressed: flash %s", buttons[color].name);
            ESP_ERROR_CHECK(blink_once_color(led, color));
        }
    }
}
