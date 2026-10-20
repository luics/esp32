// GPIO 驱动用于配置和控制 ESP32 的通用输入输出引脚。
#include "driver/gpio.h"
// ESP_ERROR_CHECK 用于检查 ESP-IDF API 的返回值；出错时会打印错误并终止程序。
#include "esp_err.h"
// ESP_LOGI 用于输出普通级别的运行状态日志。
#include "esp_log.h"
// FreeRTOS 基础定义，以及任务延时 API。
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

// 日志标签会出现在日志行中，便于筛选本示例输出。
static const char *TAG = "blink";

// ESP-IDF 应用入口。FreeRTOS 启动后会调用此函数。
void app_main(void)
{
    // 将整数形式的工程配置转换为 GPIO API 使用的引脚编号类型。
    const gpio_num_t led_gpio = (gpio_num_t)CONFIG_BLINK_GPIO;
    ESP_ERROR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(led_gpio) ? ESP_OK : ESP_ERR_INVALID_ARG);

    // 清除该引脚先前可能存在的配置，回到 GPIO 的默认状态。
    // 检查返回值，避免后续在配置失败的引脚上继续操作。
    ESP_ERROR_CHECK(gpio_reset_pin(led_gpio));

    // 把引脚配置成输出模式，这样程序才能主动输出高电平或低电平。
    ESP_ERROR_CHECK(gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT));

    // 输出当前使用的 GPIO 编号，便于确认 menuconfig 中的配置。
    ESP_LOGI(TAG, "Blinking LED on GPIO %d", CONFIG_BLINK_GPIO);

    // 持续循环：输出高电平、等待半秒、输出低电平、再等待半秒。
    // 对于阳极经限流电阻接 GPIO、阴极接 GND 的普通 LED，高电平通常点亮，
    // 低电平熄灭；若开发板 LED 为低电平有效，请交换下面两个电平的含义。
    while (true) {
        // 输出高电平，点亮高电平有效的 LED。
        ESP_ERROR_CHECK(gpio_set_level(led_gpio, 1));

        // vTaskDelay 接收 FreeRTOS tick 数；pdMS_TO_TICKS 将毫秒换算为 tick，
        // 避免直接假设系统 tick 频率。延时期间任务让出 CPU 给其他任务。
        vTaskDelay(pdMS_TO_TICKS(500));

        // 输出低电平，熄灭高电平有效的 LED。
        ESP_ERROR_CHECK(gpio_set_level(led_gpio, 0));

        // 保持熄灭 500 毫秒。与上面的延时相加后，一个闪烁周期约为 1 秒。
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
