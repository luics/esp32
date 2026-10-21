#include "blink.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "blink";

/* 在当前任务中持续闪烁 WS2812；正常运行时不返回，失败时释放驱动并返回错误码。 */
esp_err_t blink_run(int gpio_number)
{
    /* 确认引脚支持输出，避免使用无效编号或仅支持输入的 GPIO。 */
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio_number), ESP_ERR_INVALID_ARG,
                        TAG, "Invalid output GPIO %d", gpio_number);

    /* C6-Zero 板载单颗 WS2812，按微雪说明使用 RGB 数据顺序。 */
    const led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_number,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    /* RMT 产生 WS2812 所需时序，10 MHz 分辨率对应每个节拍 100 ns。 */
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    led_strip_handle_t strip = NULL;
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip),
                        TAG, "Failed to create WS2812 driver");

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(led_strip_clear(strip), cleanup, TAG, "Failed to clear LED");
    ESP_LOGI(TAG, "Blinking WS2812 on GPIO %d (RGB, blue, 500 ms on/off)", gpio_number);

    while (true) {
        /* 蓝光使用 64/255 亮度，便于观察；蓝色通道在 RGB/GRB 顺序中位置相同。 */
        ESP_GOTO_ON_ERROR(led_strip_set_pixel(strip, 0, 0, 0, 64), cleanup,
                          TAG, "Failed to set LED color");
        /* 设置颜色只更新缓冲区，refresh 才会发送到灯珠。 */
        ESP_GOTO_ON_ERROR(led_strip_refresh(strip), cleanup, TAG, "Failed to turn LED on");
        /* 将毫秒转换为系统节拍；延时期间让出 CPU，供其他任务运行。 */
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_GOTO_ON_ERROR(led_strip_clear(strip), cleanup, TAG, "Failed to turn LED off");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

cleanup:
    /* 保留最初的错误码，同时释放 RMT 通道和驱动内存。 */
    const esp_err_t cleanup_ret = led_strip_del(strip);
    if (cleanup_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to delete LED driver: %s", esp_err_to_name(cleanup_ret));
    }
    return ret;
}
