#include "blink.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "blink";

/* 在当前任务中持续闪灯；正常运行时不返回，参数无效或 GPIO 操作失败时返回错误码。 */
esp_err_t blink_run(int gpio_number)
{
    /* 确认引脚支持输出，避免使用无效编号或仅支持输入的 GPIO。 */
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio_number), ESP_ERR_INVALID_ARG,
                        TAG, "Invalid output GPIO %d", gpio_number);

    const gpio_num_t led_gpio = (gpio_num_t)gpio_number;
    /* 重置引脚配置，再将其设置为普通 GPIO 输出。 */
    ESP_RETURN_ON_ERROR(gpio_reset_pin(led_gpio), TAG, "Failed to reset GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT), TAG,
                        "Failed to configure GPIO output");

    ESP_LOGI(TAG, "Blinking LED on GPIO %d", gpio_number);

    while (true) {
        /* 外接普通 LED 采用高电平点亮：亮、灭各保持 500 毫秒。 */
        ESP_RETURN_ON_ERROR(gpio_set_level(led_gpio, 1), TAG, "Failed to turn LED on");
        /* 将毫秒转换为系统节拍；延时期间让出 CPU，供其他任务运行。 */
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_RETURN_ON_ERROR(gpio_set_level(led_gpio, 0), TAG, "Failed to turn LED off");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
