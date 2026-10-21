#include <stdbool.h>
#include "volume_control.h"
#include "beep.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "volume_control";
static adc_oneshot_unit_handle_t adc_handle;
static adc_channel_t adc_channel;
static TaskHandle_t sample_task;
static TickType_t sample_interval;
static uint8_t last_volume = UINT8_MAX;

static void potentiometer_task(void *context)
{
    (void)context;
    while (true) {
        int raw = 0;
        const esp_err_t ret = adc_oneshot_read(adc_handle, adc_channel, &raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        } else {
            /* 12 位 ADC 结果线性映射到 0~100%，避免细小抖动反复更新 PWM。 */
            const uint8_t volume = (uint8_t)((raw * 100 + 2047) / 4095);
            const int difference = (int)volume - (int)last_volume;
            const bool endpoint_changed = (volume == 0 || volume == 100) && volume != last_volume;
            if (last_volume == UINT8_MAX || difference >= 2 || difference <= -2 || endpoint_changed) {
                const esp_err_t set_ret = beep_set_volume(volume);
                if (set_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to update beep volume: %s", esp_err_to_name(set_ret));
                } else {
                    last_volume = volume;
                    ESP_LOGI(TAG, "Volume: %u%% (ADC %d)", volume, raw);
                }
            }
        }
        vTaskDelay(sample_interval);
    }
}

esp_err_t volume_control_start(int gpio_number, uint32_t sample_interval_ms)
{
    ESP_RETURN_ON_FALSE(!sample_task, ESP_ERR_INVALID_STATE, TAG, "Already started");
    ESP_RETURN_ON_FALSE(sample_interval_ms >= 10 && sample_interval_ms <= 1000,
                        ESP_ERR_INVALID_ARG, TAG, "Sample interval must be 10~1000 ms");

    adc_unit_t unit_id;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(gpio_number, &unit_id, &adc_channel),
                        TAG, "GPIO is not an ADC input");
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit_id,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &adc_handle),
                        TAG, "Failed to initialize ADC");

    const adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    esp_err_t ret = adc_oneshot_config_channel(adc_handle, adc_channel, &channel_config);
    if (ret != ESP_OK) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }

    sample_interval = pdMS_TO_TICKS(sample_interval_ms);
    if (sample_interval == 0) {
        sample_interval = 1;
    }
    /* 音量先同步为当前旋钮位置，再启动轮询，确保首次按钮鸣叫使用正确音量。 */
    int raw = 0;
    ret = adc_oneshot_read(adc_handle, adc_channel, &raw);
    if (ret != ESP_OK) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }
    last_volume = (uint8_t)((raw * 100 + 2047) / 4095);
    ret = beep_set_volume(last_volume);
    if (ret != ESP_OK) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }

    if (xTaskCreate(potentiometer_task, "beep_volume", 3072, NULL, 4, &sample_task) != pdPASS) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        sample_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Ready: GPIO %d, polling every %lu ms", gpio_number,
             (unsigned long)sample_interval_ms);
    return ESP_OK;
}

