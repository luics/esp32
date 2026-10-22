#include "beep.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "beep";
static bool initialized;

static esp_err_t set_duty(uint32_t duty)
{
    /* 由应用发送任务串行控制，无需跨任务修改 PWM。 */
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty),
                        TAG, "Failed to set PWM duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
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

esp_err_t beep_init(int gpio_number, uint32_t frequency_hz)
{
    ESP_RETURN_ON_FALSE(!initialized, ESP_ERR_INVALID_STATE, TAG, "Buzzer already initialized");
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio_number)
                        && frequency_hz >= 100 && frequency_hz <= 10000,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid buzzer configuration");
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

    initialized = true;
    ESP_LOGI(TAG, "Ready: GPIO %d, %lu Hz", gpio_number, (unsigned long)frequency_hz);
    return ESP_OK;
}

esp_err_t beep_set_active(bool active)
{
    ESP_RETURN_ON_FALSE(initialized, ESP_ERR_INVALID_STATE, TAG, "Buzzer not initialized");
    /* 与灯珠由同一个任务控制，点划的开始、结束共享计时。 */
    const esp_err_t ret = set_duty(active ? 512 : 0);
    if (ret != ESP_OK) {
        const esp_err_t stop_ret = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        if (stop_ret != ESP_OK) ESP_LOGE(TAG, "Failed to stop buzzer: %s", esp_err_to_name(stop_ret));
    }
    return ret;
}
