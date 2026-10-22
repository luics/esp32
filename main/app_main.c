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
#include "telegraph.h"
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
static atomic_bool event_overflow;
typedef enum { RED_DOWN, RED_UP, HISTORY_NEXT } input_kind_t;
typedef struct {
    input_kind_t kind;
    uint64_t at_ms;
    uint32_t selected_unit_ms;
} input_event_t;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static void post_input(QueueHandle_t queue, input_kind_t kind)
{
    /* 驱动回调只记录边沿及时间，不等待发声或显示；队列满时由主任务恢复按键状态。 */
    const input_event_t event = {
        .kind = kind, .at_ms = now_ms(), .selected_unit_ms = interval_control_get_unit_ms(),
    };
    if (xQueueSend(queue, &event, 0) != pdTRUE) atomic_store(&event_overflow, true);
}

static void on_red_edge(bool pressed, void *context)
{
    post_input((QueueHandle_t)context, pressed ? RED_DOWN : RED_UP);
}

static void on_history_press(void *context)
{
    post_input((QueueHandle_t)context, HISTORY_NEXT);
}

static void on_reset_press(void *context)
{
    /* 复位通知独立于点划 FIFO；按钮回调只通知，不在驱动任务中重启。 */
    if (atomic_load(&reset_armed)) xTaskNotifyGive((TaskHandle_t)context);
}

static esp_err_t stop_feedback(blink_handle_t led)
{
    /* 松开、事件溢出及复位共用同一停声灭灯顺序，出现错误也尝试关闭两者。 */
    const esp_err_t sound_ret = beep_set_active(false);
    const esp_err_t led_ret = blink_off(led);
    if (sound_ret != ESP_OK) ESP_LOGW(TAG, "Mute failed: %s", esp_err_to_name(sound_ret));
    if (led_ret != ESP_OK) ESP_LOGW(TAG, "LED off failed: %s", esp_err_to_name(led_ret));
    return sound_ret != ESP_OK ? sound_ret : led_ret;
}

static void restart_if_requested(blink_handle_t led)
{
    /* 上电时仍按住蓝键则忽略，松开后才接受下一次按下，防止循环重启。 */
    if (gpio_get_level(CONFIG_BUTTON_INPUT_BLUE_GPIO)) atomic_store(&reset_armed, true);
    if (ulTaskNotifyTake(pdTRUE, 0) == 0) return;
    ESP_LOGI(TAG, "Blue reset: restarting through app_main initialization");
    (void)stop_feedback(led);
    /* 统一复用现有启动流程，避免单独维护一份状态与外设重置逻辑。 */
    esp_restart();
}

static void show_state(display_handle_t screen, const telegraph_t *telegraph,
                       uint32_t selected, size_t page, display_frame_t *frame)
{
    const morse_t *morse = &telegraph->morse;
    memset(frame, 0, sizeof(*frame));
    frame->selected_unit_ms = selected;
    frame->character_unit_ms = telegraph->pressed ? telegraph->pressed_unit_ms
                                                : (morse->current[0] ? morse->unit_ms : selected);
    strcpy(frame->current_code, morse->invalid ? "INVALID" : morse->current);
    frame->decoded = morse->current[0] ? (morse->invalid ? '?' : morse_decode(morse->current)) : ' ';
    frame->characters = morse->count;
    frame->pages = morse_history_lines(morse, page, DISPLAY_HISTORY_ROWS, DISPLAY_HISTORY_COLS, frame->history);
    frame->page = page + 1;
    ESP_ERROR_CHECK(display_show_frame(screen, frame));
}

void app_main(void)
{
    const display_config_t screen_config = {
        .sclk_gpio = CONFIG_DISPLAY_SCLK_GPIO, .mosi_gpio = CONFIG_DISPLAY_MOSI_GPIO,
        .cs_gpio = CONFIG_DISPLAY_CS_GPIO, .dc_gpio = CONFIG_DISPLAY_DC_GPIO,
        .reset_gpio = CONFIG_DISPLAY_RST_GPIO,
    };
    /* 上电前统一检查引脚，避免任何外设初始化覆盖其他设备。 */
    const int pins[] = {CONFIG_BLINK_RGB_GPIO, CONFIG_BEEP_GPIO, CONFIG_INTERVAL_CONTROL_ADC_GPIO,
                        CONFIG_BUTTON_INPUT_RED_GPIO, CONFIG_BUTTON_INPUT_GPIO, CONFIG_BUTTON_INPUT_BLUE_GPIO, screen_config.sclk_gpio,
                        screen_config.mosi_gpio,
#ifndef CONFIG_DISPLAY_WOKWI_SH1107
                        screen_config.cs_gpio, screen_config.dc_gpio, screen_config.reset_gpio,
#endif
    };
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
    QueueHandle_t events = xQueueCreate(64, sizeof(input_event_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    button_input_handle_t red_button = NULL, history_button = NULL, reset_button = NULL;
    ESP_ERROR_CHECK(button_input_create_edges(CONFIG_BUTTON_INPUT_RED_GPIO, on_red_edge,
                                              events, &red_button));
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_GPIO, on_history_press,
                                        events, &history_button));
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_BLUE_GPIO, on_reset_press,
                                        xTaskGetCurrentTaskHandle(), &reset_button));

    telegraph_t telegraph = {0};
    display_frame_t frame;
    size_t page = 0;
    bool dash_feedback = false;
    bool wait_red_release = false;
    uint32_t selected = interval_control_get_unit_ms();
    show_state(screen, &telegraph, selected, page, &frame);
    ESP_LOGI(TAG, "Ready: red down=light/sound on, up=off/decode; <3 units=dot, >=3=dash; green=page");
    while (true) {
        restart_if_requested(led);
        const size_t before = telegraph.morse.count;
        const bool last_word_end = before && telegraph.morse.history[before - 1].word_end;
        bool changed = false;
        unsigned page_requests = 0;
        if (atomic_exchange(&event_overflow, false)) {
            /* 丢失松开事件时取消未完成按键并等物理松开，等待松开后再接收下一次输入。 */
            ESP_ERROR_CHECK(stop_feedback(led));
            xQueueReset(events);
            telegraph.pressed = false;
            wait_red_release = !gpio_get_level(CONFIG_BUTTON_INPUT_RED_GPIO);
            ESP_LOGW(TAG, "Input event queue overflow: muted and canceled incomplete press");
        }
        if (wait_red_release && gpio_get_level(CONFIG_BUTTON_INPUT_RED_GPIO)) wait_red_release = false;
        input_event_t event;
        /* 先处理已采集边沿再结算静默，避免长按在三个单位临界处被拆分字符。 */
        for (unsigned i = 0; i < 64 && xQueueReceive(events, &event, 0) == pdTRUE; ++i) {
            if (event.kind == HISTORY_NEXT) {
                ++page_requests;
            } else if (!wait_red_release) {
                if (event.kind == RED_DOWN && !telegraph.pressed) {
                    ESP_ERROR_CHECK(telegraph_press(&telegraph, event.at_ms, event.selected_unit_ms)
                                     ? ESP_OK : ESP_ERR_NO_MEM);
                    /* 消抖后的按下立即接通声光，持续到本次松开，不等待点划判定。 */
                    ESP_ERROR_CHECK(blink_set_color(led, BLINK_COLOR_RED));
                    const esp_err_t sound_ret = beep_set_active(true);
                    if (sound_ret != ESP_OK) {
                        ESP_ERROR_CHECK(blink_off(led));
                        ESP_ERROR_CHECK(sound_ret);
                    }
                    dash_feedback = false;
                } else if (event.kind == RED_UP) {
                    /* 先停声灭灯，再用松开边沿时刻识别；静默从实际关闭声光起算。 */
                    ESP_ERROR_CHECK(stop_feedback(led));
                    const uint64_t stopped_ms = now_ms();
                    const char symbol = telegraph_release(&telegraph, event.at_ms);
                    if (symbol) {
                        morse_end(&telegraph.morse, stopped_ms);
                        ESP_LOGI(TAG, "Released: %c, held %llu ms", symbol,
                                 (unsigned long long)(event.at_ms - telegraph.pressed_ms));
                    }
                }
                changed = true;
            }
        }
        ESP_ERROR_CHECK(telegraph_advance(&telegraph, now_ms()) ? ESP_OK : ESP_ERR_NO_MEM);
        const size_t after = telegraph.morse.count;
        const bool next_word_end = after && telegraph.morse.history[after - 1].word_end;
        if (before != after || last_word_end != next_word_end) {
            /* 新记录/词分组回到最新页；其余时间仅由绿键翻页，不再自动轮播。 */
            page = 0;
            changed = true;
            if (before != after) {
                const morse_entry_t *entry = &telegraph.morse.history[after - 1];
                ESP_LOGI(TAG, "History %u: %s = %c", (unsigned)after, entry->code, entry->decoded);
            }
        }
        if (page_requests) {
            const size_t pages = morse_history_lines(&telegraph.morse, 0, DISPLAY_HISTORY_ROWS,
                                                     DISPLAY_HISTORY_COLS, frame.history);
            page = (page + page_requests) % pages;
            changed = true;
        }
        const uint32_t next_selected = interval_control_get_unit_ms();
        changed |= selected != next_selected;
        selected = next_selected;
        /* 长按达到三单位只切换灯色提示；蜂鸣器保持连续，点划仍在松开后提交。 */
        const uint64_t now = now_ms();
        if (telegraph.pressed && !dash_feedback && now >= telegraph.pressed_ms &&
            now - telegraph.pressed_ms >= 3ULL * telegraph.pressed_unit_ms) {
            ESP_ERROR_CHECK(blink_set_color(led, BLINK_COLOR_GREEN));
            dash_feedback = true;
        }
        if (changed) show_state(screen, &telegraph, selected, page, &frame);
        vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
    }
}
