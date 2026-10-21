#include "blink.h"
#include "button_input.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "app";

static void on_button_press(void *context)
{
    /* 驱动回调只发送计数通知，闪光延时由主任务承担。 */
    xTaskNotifyGive((TaskHandle_t)context);
}

void app_main(void)
{
    /* 按钮输入与灯珠输出必须使用不同引脚，避免初始化时互相覆盖。 */
    ESP_ERROR_CHECK(CONFIG_BUTTON_INPUT_GPIO == CONFIG_BLINK_RGB_GPIO
                    ? ESP_ERR_INVALID_ARG : ESP_OK);
    /* 先初始化灯珠并熄灭，等待按钮事件后再闪光。 */
    blink_handle_t led = NULL;
    ESP_ERROR_CHECK(blink_create(CONFIG_BLINK_RGB_GPIO, &led));
    /* 将当前主任务作为通知接收方，按钮回调无需直接操作灯珠。 */
    button_input_handle_t button = NULL;
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_GPIO, on_button_press,
                                        xTaskGetCurrentTaskHandle(), &button));
    ESP_LOGI(TAG, "Ready: press GPIO %d button to flash GPIO %d LED",
             CONFIG_BUTTON_INPUT_GPIO, CONFIG_BLINK_RGB_GPIO);
    while (true) {
        /* 每次取走一个通知；闪光期间的新按下会排队，长按不重复触发。 */
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        ESP_LOGI(TAG, "Button pressed: flash once");
        /* 串行执行一次亮灭；等待期间按钮驱动仍可继续检测新的按下。 */
        ESP_ERROR_CHECK(blink_once(led));
    }
}
