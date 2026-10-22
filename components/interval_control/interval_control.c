#include <stdbool.h>
#include "interval_control.h"
#include <stdatomic.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "interval_control";
static adc_oneshot_unit_handle_t adc_handle;
static adc_channel_t adc_channel;
static TaskHandle_t sample_task;
static TickType_t sample_interval;
static atomic_uint current_unit_ms = 200;
static int current_band;
static const uint32_t units[] = {200, 150, 100, 60};

uint32_t interval_control_get_unit_ms(void)
{
    return atomic_load(&current_unit_ms);
}

static void update_interval(int raw, bool initial)
{
    int band = current_band;
    if (initial) {
        band = raw / 1024;
    } else {
        /* 阈值两侧各留 80 ADC 计数，抑制旋钮临界位置的档位抖动。 */
        while (band < 3 && raw >= (band + 1) * 1024 + 80) ++band;
        while (band > 0 && raw < band * 1024 - 80) --band;
    }
    if (initial || band != current_band) {
        current_band = band;
        atomic_store(&current_unit_ms, units[band]);
        ESP_LOGI(TAG, "Interval: %lu ms (ADC %d)", (unsigned long)units[band], raw);
    }
}

static void potentiometer_task(void *context)
{
    (void)context;
    while (true) {
        int raw = 0;
        const esp_err_t ret = adc_oneshot_read(adc_handle, adc_channel, &raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        } else {
            update_interval(raw, false);
        }
        vTaskDelay(sample_interval);
    }
}

esp_err_t interval_control_start(int gpio_number, uint32_t sample_interval_ms)
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
    /* 启动时读取实际档位；Wokwi 旋钮默认在最低端，对应 200 ms。 */
    int raw = 0;
    ret = adc_oneshot_read(adc_handle, adc_channel, &raw);
    if (ret != ESP_OK) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }
    update_interval(raw, true);

    if (xTaskCreate(potentiometer_task, "interval_adc", 3072, NULL, 4, &sample_task) != pdPASS) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        sample_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Ready: GPIO %d, polling every %lu ms", gpio_number,
             (unsigned long)sample_interval_ms);
    return ESP_OK;
}

