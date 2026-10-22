#include <stdio.h>
#include <stdatomic.h>
#include "driver/gpio.h"
#include <string.h>
#include "blink.h"
#include "beep.h"
#include "display.h"
#include "button_input.h"
#include "interval_control.h"
#include "morse.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "telegraph";
static atomic_bool reset_armed;
typedef struct {
    int gpio;
    char symbol;
    QueueHandle_t events;
    button_input_handle_t handle;
} morse_button_t;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static void on_button_press(void *context)
{
    const morse_button_t *button = context;
    /* FIFO 保留每次点击；回调不等待声音或屏幕，长按由按钮驱动抑制。 */
    if (xQueueSend(button->events, &button->symbol, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Input queue full: rejected %c", button->symbol);
    }
}

static void on_reset_press(void *context)
{
    /* 复位通知独立于点划 FIFO；按钮回调只通知，不在驱动任务中重启。 */
    if (atomic_load(&reset_armed)) xTaskNotifyGive((TaskHandle_t)context);
}

static void restart_if_requested(blink_handle_t led)
{
    /* 上电时仍按住蓝键则忽略，松开后才接受下一次按下，防止循环重启。 */
    if (gpio_get_level(CONFIG_BUTTON_INPUT_BLUE_GPIO)) atomic_store(&reset_armed, true);
    if (ulTaskNotifyTake(pdTRUE, 0) == 0) return;
    ESP_LOGI(TAG, "Blue reset: restarting through app_main initialization");
    const esp_err_t sound_ret = beep_set_active(false);
    const esp_err_t led_ret = blink_off(led);
    if (sound_ret != ESP_OK) ESP_LOGW(TAG, "Reset mute failed: %s", esp_err_to_name(sound_ret));
    if (led_ret != ESP_OK) ESP_LOGW(TAG, "Reset LED off failed: %s", esp_err_to_name(led_ret));
    /* 统一复用现有启动流程，避免单独维护一份状态与外设重置逻辑。 */
    esp_restart();
}

static void show_state(display_handle_t screen, const morse_t *morse,
                       uint32_t selected, size_t page, display_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->selected_unit_ms = selected;
    frame->character_unit_ms = morse->current[0] ? morse->unit_ms : selected;
    strcpy(frame->current_code, morse->invalid ? "INVALID" : morse->current);
    frame->decoded = morse->current[0] ? (morse->invalid ? '?' : morse_decode(morse->current)) : ' ';
    frame->characters = morse->count;
    frame->pages = morse_history_lines(morse, page, frame->history);
    frame->page = page + 1;
    ESP_ERROR_CHECK(display_show_frame(screen, frame));
}

void app_main(void)
{
    morse_button_t buttons[] = {
        {.gpio = CONFIG_BUTTON_INPUT_RED_GPIO, .symbol = '.'},
        {.gpio = CONFIG_BUTTON_INPUT_GPIO, .symbol = '-'},
    };
    const display_config_t screen_config = {
        .sclk_gpio = CONFIG_DISPLAY_SCLK_GPIO, .mosi_gpio = CONFIG_DISPLAY_MOSI_GPIO,
        .cs_gpio = CONFIG_DISPLAY_CS_GPIO, .dc_gpio = CONFIG_DISPLAY_DC_GPIO,
        .reset_gpio = CONFIG_DISPLAY_RST_GPIO,
    };
    /* 上电前统一检查引脚，避免任何外设初始化覆盖其他设备。 */
    const int pins[] = {CONFIG_BLINK_RGB_GPIO, CONFIG_BEEP_GPIO, CONFIG_INTERVAL_CONTROL_ADC_GPIO,
                        buttons[0].gpio, buttons[1].gpio, CONFIG_BUTTON_INPUT_BLUE_GPIO, screen_config.sclk_gpio,
                        screen_config.mosi_gpio, screen_config.cs_gpio, screen_config.dc_gpio,
                        screen_config.reset_gpio};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        for (size_t j = 0; j < i; ++j) ESP_ERROR_CHECK(pins[i] == pins[j] ? ESP_ERR_INVALID_ARG : ESP_OK);
    }
    display_handle_t screen = NULL;
    blink_handle_t led = NULL;
    ESP_ERROR_CHECK(display_init(&screen_config, &screen));
    ESP_ERROR_CHECK(blink_create(CONFIG_BLINK_RGB_GPIO, &led));
    ESP_ERROR_CHECK(beep_init(CONFIG_BEEP_GPIO, CONFIG_BEEP_FREQUENCY_HZ));
    ESP_ERROR_CHECK(interval_control_start(CONFIG_INTERVAL_CONTROL_ADC_GPIO,
                                           CONFIG_INTERVAL_CONTROL_SAMPLE_INTERVAL_MS));
    QueueHandle_t events = xQueueCreate(32, sizeof(char));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        buttons[i].events = events;
        ESP_ERROR_CHECK(button_input_create(buttons[i].gpio, on_button_press,
                                            &buttons[i], &buttons[i].handle));
    }

    button_input_handle_t reset_button = NULL;
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_BLUE_GPIO, on_reset_press,
                                        xTaskGetCurrentTaskHandle(), &reset_button));

    morse_t morse = {0};
    display_frame_t frame;
    size_t page = 0;
    uint64_t flip_at = now_ms() + 4000;
    uint32_t selected = interval_control_get_unit_ms();
    show_state(screen, &morse, selected, page, &frame);
    ESP_LOGI(TAG, "Ready: red=dot, green=dash, blue=reset; 3 units=letter, 7 units=word");
    while (true) {
        restart_if_requested(led);
        const size_t before = morse.count;
        const bool word_before = morse.word_done;
        ESP_ERROR_CHECK(morse_advance(&morse, now_ms()) ? ESP_OK : ESP_ERR_NO_MEM);
        bool changed = before != morse.count || word_before != morse.word_done;
        if (before != morse.count) {
            const morse_entry_t *entry = &morse.history[morse.count - 1];
            ESP_LOGI(TAG, "History %u: %s = %c", (unsigned)morse.count, entry->code, entry->decoded);
        }
        if (changed) {
            /* 新历史先定位到最新页；随后自动循环浏览全部历史。 */
            page = morse_history_lines(&morse, 0, frame.history) - 1;
            flip_at = now_ms() + 4000;
        }
        const uint32_t next_selected = interval_control_get_unit_ms();
        changed |= selected != next_selected;
        selected = next_selected;
        if (now_ms() >= flip_at) {
            page = (page + 1) % frame.pages;
            flip_at = now_ms() + 4000;
            changed = true;
        }
        if (changed) show_state(screen, &morse, selected, page, &frame);

        char symbol;
        if (xQueueReceive(events, &symbol, pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1) != pdTRUE) continue;
        /* 最少留一个单位静默，快速点击排队发送，不覆盖或截断上一个信号。 */
        while (morse.has_signal && now_ms() < morse.ended_ms + morse.unit_ms) {
            restart_if_requested(led);
            vTaskDelay(1);
        }
        restart_if_requested(led);
        /* 收到按键时再次结算边界，避免在三/七单位临界处把字符错误合并。 */
        const size_t old_count = morse.count;
        const bool old_word = morse.word_done;
        ESP_ERROR_CHECK(morse_advance(&morse, now_ms()) ? ESP_OK : ESP_ERR_NO_MEM);
        if (old_count != morse.count || old_word != morse.word_done) {
            page = morse_history_lines(&morse, 0, frame.history) - 1;
            flip_at = now_ms() + 4000;
            if (old_count != morse.count) {
                const morse_entry_t *entry = &morse.history[morse.count - 1];
                ESP_LOGI(TAG, "History %u: %s = %c", (unsigned)morse.count, entry->code, entry->decoded);
            }
        }
        morse_begin(&morse, symbol, interval_control_get_unit_ms());
        show_state(screen, &morse, selected, page, &frame);
        const uint32_t duration = morse.unit_ms * (symbol == '.' ? 1 : 3);
        ESP_ERROR_CHECK(blink_set_color(led, symbol == '.' ? BLINK_COLOR_RED : BLINK_COLOR_GREEN));
        const esp_err_t sound_ret = beep_set_active(true);
        if (sound_ret != ESP_OK) {
            ESP_ERROR_CHECK(blink_off(led));
            ESP_ERROR_CHECK(sound_ret);
        }
        /* 定长播放：按住按钮不延长，旋钮变化也不改动当前字符的时序。 */
        const uint64_t end = now_ms() + duration;
        while (now_ms() < end) {
            restart_if_requested(led);
            vTaskDelay(1);
        }
        const esp_err_t stop_ret = beep_set_active(false);
        ESP_ERROR_CHECK(blink_off(led));
        ESP_ERROR_CHECK(stop_ret);
        morse_end(&morse, now_ms());
        ESP_LOGI(TAG, "Sent %c, %lu ms", symbol, (unsigned long)duration);
    }
}
