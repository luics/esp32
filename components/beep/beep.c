#include "beep.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "beep";
static TaskHandle_t beep_task;
static TickType_t beep_duration;

static esp_err_t set_duty(uint32_t duty)
{
    /* 仅专用任务更新占空比，无需安装 LEDC 渐变服务或跨任务加锁。 */
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty),
                        TAG, "Failed to set PWM duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void beep_worker(void *context)
{
    (void)context;
    while (true) {
        /* 空闲时等待按钮通知；PWM 的开关只由本任务操作。 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        esp_err_t ret = set_duty(512);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start beep: %s", esp_err_to_name(ret));
        } else {
            /* 等待期间再次按下会唤醒并重新计时，不排队播放过期声音。 */
            while (ulTaskNotifyTake(pdTRUE, beep_duration) != 0) {
            }
        }
        ret = set_duty(0);
        if (ret != ESP_OK) {
            /* 更新占空比失败时，额外尝试将输出停止在低电平。 */
            ESP_LOGE(TAG, "Failed to silence beep: %s", esp_err_to_name(ret));
            ret = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stop buzzer: %s", esp_err_to_name(ret));
            }
        }
    }
}

static void release_timer(void)
{
    /* 初始化失败时回收本组件独占的定时器，清理错误不覆盖最初错误。 */
    esp_err_t ret = ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    if (ret == ESP_OK) {
        const ledc_timer_config_t config = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = LEDC_TIMER_0,
            .deconfigure = true,
        };
        ret = ledc_timer_config(&config);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to release LEDC timer: %s", esp_err_to_name(ret));
    }
}

esp_err_t beep_init(int gpio_number, uint32_t frequency_hz, uint32_t duration_ms)
{
    ESP_RETURN_ON_FALSE(!beep_task, ESP_ERR_INVALID_STATE, TAG, "Buzzer already initialized");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio_number)
                        && frequency_hz >= 100 && frequency_hz <= 10000
                        && duration_ms >= 1 && duration_ms <= 60000,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid buzzer configuration");
    beep_duration = pdMS_TO_TICKS(duration_ms);
    if (beep_duration == 0) {
        beep_duration = 1;
    }

    /* 先配置 10 位 PWM 定时器，再绑定 GPIO；占空比为零确保上电静音。 */
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "Failed to configure LEDC timer");
    const ledc_channel_config_t channel = {
        .gpio_num = gpio_number,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    esp_err_t ret = ledc_channel_config(&channel);
    if (ret != ESP_OK) {
        release_timer();
        return ret;
    }

    /* 创建专用任务，使蜂鸣器的计时不阻塞按钮回调或闪灯主任务。 */
    if (xTaskCreate(beep_worker, "beep", 2048, NULL, 5, &beep_task) != pdPASS) {
        ret = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop LEDC during cleanup: %s", esp_err_to_name(ret));
        }
        release_timer();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Ready: GPIO %d, %lu Hz, %lu ms", gpio_number,
             (unsigned long)frequency_hz, (unsigned long)duration_ms);
    return ESP_OK;
}

esp_err_t beep_trigger(void)
{
    ESP_RETURN_ON_FALSE(beep_task, ESP_ERR_INVALID_STATE, TAG, "Buzzer not initialized");
    /* 通知由任务上下文发送，立即返回，声音由后台任务处理。 */
    xTaskNotifyGive(beep_task);
    return ESP_OK;
}
