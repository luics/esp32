#include "button_input.h"
#include <stdlib.h>
#include "button_gpio.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "iot_button.h"

static const char *TAG = "button_input";

struct button_input {
    button_handle_t driver;
    int gpio_number;
    button_input_callback_t callback;
    button_input_edge_callback_t edge_callback;
    void *context;
};

static void on_press(void *button, void *user_data)
{
    (void)button;
    /* 将驱动事件转交给应用回调，透传创建时保存的上下文。 */
    const struct button_input *input = user_data;
    /* 在消抖按下时统一记录，覆盖全部按钮且不随长按重复发送。 */
    ESP_LOGI(TAG, "Click: GPIO%d", input->gpio_number);
    if (input->edge_callback) input->edge_callback(true, input->context);
    else input->callback(input->context);
}

static void on_release(void *button, void *user_data)
{
    (void)button;
    const struct button_input *input = user_data;
    input->edge_callback(false, input->context);
}

static esp_err_t create_input(int gpio_number, button_input_callback_t callback,
                              button_input_edge_callback_t edge_callback,
                              void *context, button_input_handle_t *out_handle)
{
    /* 先校验参数并清空输出，确保失败时调用方不会拿到无效句柄。 */
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_INVALID_ARG, TAG, "Missing output handle");
    *out_handle = NULL;
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(gpio_number) && (callback || edge_callback),
                        ESP_ERR_INVALID_ARG, TAG, "Invalid button configuration");
    /* 保存回调及上下文，供后续每次按下时使用。 */
    button_input_handle_t input = calloc(1, sizeof(*input));
    ESP_RETURN_ON_FALSE(input, ESP_ERR_NO_MEM, TAG, "Failed to allocate button");
    input->gpio_number = gpio_number;
    input->callback = callback;
    input->edge_callback = edge_callback;
    input->context = context;
    /* 使用驱动默认的扫描与消抖参数；低电平有效时默认开启内部上拉。 */
    const button_config_t config = {0};
    const button_gpio_config_t gpio_config = {
        .gpio_num = gpio_number,
        .active_level = 0,
    };
    esp_err_t ret = iot_button_new_gpio_device(&config, &gpio_config, &input->driver);
    if (ret != ESP_OK) {
        free(input);
        return ret;
    }
    /* 按下即通知，避免单击事件等待双击判定，也保留连续点击的每次触发。 */
    ret = iot_button_register_cb(input->driver, BUTTON_PRESS_DOWN, NULL, on_press, input);
    if (ret == ESP_OK && edge_callback) {
        ret = iot_button_register_cb(input->driver, BUTTON_PRESS_UP, NULL, on_release, input);
    }
    if (ret != ESP_OK) {
        /* 注册失败时释放驱动；只有驱动删除成功后才能释放其引用的上下文。 */
        const esp_err_t cleanup_ret = iot_button_delete(input->driver);
        if (cleanup_ret == ESP_OK) {
            free(input);
        } else {
            ESP_LOGW(TAG, "Failed to delete button: %s", esp_err_to_name(cleanup_ret));
        }
        return ret;
    }
    /* 初始化和回调注册均成功后，才将句柄交给调用方。 */
    *out_handle = input;
    return ESP_OK;
}

esp_err_t button_input_create(int gpio_number, button_input_callback_t callback,
                              void *context, button_input_handle_t *out_handle)
{
    return create_input(gpio_number, callback, NULL, context, out_handle);
}

esp_err_t button_input_create_edges(int gpio_number, button_input_edge_callback_t callback,
                                    void *context, button_input_handle_t *out_handle)
{
    return create_input(gpio_number, NULL, callback, context, out_handle);
}
