#include "blink.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "blink";

esp_err_t blink_run(int gpio_number)
{
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio_number), ESP_ERR_INVALID_ARG,
                        TAG, "Invalid output GPIO %d", gpio_number);

    const gpio_num_t led_gpio = (gpio_num_t)gpio_number;
    ESP_RETURN_ON_ERROR(gpio_reset_pin(led_gpio), TAG, "Failed to reset GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT), TAG,
                        "Failed to configure GPIO output");

    ESP_LOGI(TAG, "Blinking LED on GPIO %d", gpio_number);

    while (true) {
        ESP_RETURN_ON_ERROR(gpio_set_level(led_gpio, 1), TAG, "Failed to turn LED on");
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_RETURN_ON_ERROR(gpio_set_level(led_gpio, 0), TAG, "Failed to turn LED off");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
