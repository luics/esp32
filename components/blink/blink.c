#include "blink.h"
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "blink";

struct blink {
    led_strip_handle_t strip;
};

/* 兼容接口沿用板载灯的构建配置；外接灯可单独指定颜色顺序。 */
esp_err_t blink_create(int gpio_number, blink_handle_t *out_handle)
{
#ifdef CONFIG_BLINK_LED_ORDER_GRB
    return blink_create_with_order(gpio_number, BLINK_ORDER_GRB, out_handle);
#else
    return blink_create_with_order(gpio_number, BLINK_ORDER_RGB, out_handle);
#endif
}

/* 初始化后立即灭灯，后续闪光复用同一个 RMT 通道。 */
esp_err_t blink_create_with_order(int gpio_number, blink_order_t order, blink_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Missing output handle");
    *out_handle = NULL;
    /* 确认引脚支持输出，避免使用无效编号或仅支持输入的 GPIO。 */
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio_number), ESP_ERR_INVALID_ARG,
                        TAG, "Invalid output GPIO %d", gpio_number);

    ESP_RETURN_ON_FALSE(order == BLINK_ORDER_RGB || order == BLINK_ORDER_GRB,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid LED color order");
    /* 每颗灯各持有一个 RMT 驱动与颜色缓冲，互不覆盖输出。 */
    const led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_number,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = order == BLINK_ORDER_GRB
            ? LED_STRIP_COLOR_COMPONENT_FMT_GRB : LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    /* RMT 产生 WS2812 所需时序，10 MHz 分辨率对应每个节拍 100 ns。 */
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    /* 创建可复用句柄，将灯珠驱动的生命周期与单次闪光分离。 */
    blink_handle_t handle = calloc(1, sizeof(*handle));
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_NO_MEM, TAG, "Failed to allocate LED");
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &handle->strip);
    if (ret != ESP_OK) {
        free(handle);
        return ret;
    }
    /* 上电先灭灯；初始化失败时清理已创建的驱动并保留最初错误码。 */
    ret = led_strip_clear(handle->strip);
    if (ret != ESP_OK) {
        const esp_err_t cleanup_ret = blink_delete(handle);
        if (cleanup_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete LED driver: %s", esp_err_to_name(cleanup_ret));
        }
        return ret;
    }
    *out_handle = handle;
    return ESP_OK;
}

esp_err_t blink_once(blink_handle_t handle)
{
    /* 保留原接口的蓝光行为，统一复用指定颜色的闪光流程。 */
    return blink_once_color(handle, BLINK_COLOR_BLUE);
}

esp_err_t blink_set_color(blink_handle_t handle, blink_color_t color)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid LED handle");
    ESP_RETURN_ON_FALSE(color >= BLINK_COLOR_RED && color <= BLINK_COLOR_YELLOW,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid LED color");
    /* 仅更新颜色并发送，时长交给调用方，以便等待过程被新按钮事件唤醒。 */
    const unsigned red = (color == BLINK_COLOR_RED || color == BLINK_COLOR_YELLOW) ? 64 : 0;
    const unsigned green = (color == BLINK_COLOR_GREEN || color == BLINK_COLOR_YELLOW) ? 64 : 0;
    const unsigned blue = color == BLINK_COLOR_BLUE ? 64 : 0;
    ESP_RETURN_ON_ERROR(led_strip_set_pixel(handle->strip, 0, red, green, blue),
                        TAG, "Failed to set LED color");
    /* 设置颜色只更新缓冲区，refresh 才会发送到灯珠。 */
    return led_strip_refresh(handle->strip);
}

esp_err_t blink_off(blink_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid LED handle");
    return led_strip_clear(handle->strip);
}

esp_err_t blink_once_color(blink_handle_t handle, blink_color_t color)
{
    /* 保留原有阻塞接口供其他调用方使用；按钮主流程改用即时控制接口。 */
    esp_err_t ret = blink_set_color(handle, color);
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    /* 亮灯结束或发送失败后均尝试灭灯；发送失败时优先返回原错误。 */
    const esp_err_t clear_ret = blink_off(handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_RETURN_ON_ERROR(clear_ret, TAG, "Failed to turn LED off");
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

esp_err_t blink_delete(blink_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid LED handle");
    /* 先释放底层驱动，成功后再释放包装句柄；失败时保留句柄以便重试。 */
    ESP_RETURN_ON_ERROR(led_strip_del(handle->strip), TAG, "Failed to delete LED driver");
    free(handle);
    return ESP_OK;
}

/* 保留原有的持续闪烁接口。 */
esp_err_t blink_run(int gpio_number)
{
    blink_handle_t handle = NULL;
    ESP_RETURN_ON_ERROR(blink_create(gpio_number, &handle), TAG, "Failed to create LED");
    esp_err_t ret;
    while ((ret = blink_once(handle)) == ESP_OK) {
    }
    /* 持续闪烁出错后释放驱动，清理结果不覆盖导致退出的错误码。 */
    const esp_err_t cleanup_ret = blink_delete(handle);
    if (cleanup_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to delete LED driver: %s", esp_err_to_name(cleanup_ret));
    }
    return ret;
}
