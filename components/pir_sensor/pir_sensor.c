#include "pir_sensor.h"
#include <limits.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "pir_sensor";
struct pir_sensor {
    gpio_num_t gpio;
    bool motion;
    uint64_t high_since_ms;
};

esp_err_t pir_sensor_create(int gpio, pir_sensor_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Missing output handle");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(gpio), ESP_ERR_INVALID_ARG, TAG, "Invalid GPIO %d", gpio);
    pir_sensor_handle_t handle = calloc(1, sizeof(*handle));
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_NO_MEM, TAG, "No memory for PIR sensor");
    /* PIR OUT 是高电平有效的数字信号；下拉确保模块未接时保持空闲。 */
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }
    handle->gpio = gpio;
    *out_handle = handle;
    return ESP_OK;
}

esp_err_t pir_sensor_sample(pir_sensor_handle_t handle, uint64_t now_ms,
                            pir_sensor_sample_t *out_sample)
{
    ESP_RETURN_ON_FALSE(handle && out_sample, ESP_ERR_INVALID_ARG, TAG, "Invalid PIR sample arguments");
    const bool motion = gpio_get_level(handle->gpio) != 0;
    const bool changed = motion != handle->motion;
    if (changed) {
        /* 只在边沿打印日志，避免长期高电平时刷屏。 */
        if (motion) {
            handle->high_since_ms = now_ms;
            ESP_LOGI(TAG, "GPIO%d motion detected", handle->gpio);
        } else {
            ESP_LOGI(TAG, "GPIO%d motion ended after %llu ms", handle->gpio,
                     (unsigned long long)(now_ms - handle->high_since_ms));
        }
        handle->motion = motion;
    }
    const uint64_t elapsed = motion ? now_ms - handle->high_since_ms : 0;
    *out_sample = (pir_sensor_sample_t){
        .motion = motion,
        .changed = changed,
        .active_ms = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed,
    };
    return ESP_OK;
}

esp_err_t pir_sensor_delete(pir_sensor_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid PIR handle");
    free(handle);
    return ESP_OK;
}
