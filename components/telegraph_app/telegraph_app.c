#include "telegraph_app.h"
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
#include "keyer.h"
#include "telegraph_sync.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "telegraph";
static atomic_bool reset_armed;
static atomic_bool event_overflow;
typedef enum { RED_DOWN, RED_UP, BLACK_DOWN, BLACK_UP, MODE_TOGGLE, HISTORY_NEXT } input_kind_t;
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

static void on_black_edge(bool pressed, void *context)
{
    post_input((QueueHandle_t)context, pressed ? BLACK_DOWN : BLACK_UP);
}

static void on_mode_press(void *context)
{
    post_input((QueueHandle_t)context, MODE_TOGGLE);
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

/* 声光始终由输入任务串行更新；双键的松开不截短信号，单键仍跟随边沿。 */
static void sync_feedback(keyer_t *keyer, blink_handle_t led, bool *active, blink_color_t *color)
{
    telegraph_t *t = &keyer->telegraph;
    const blink_color_t next_color = keyer->dual_mode
        ? (keyer->symbol == '-' ? BLINK_COLOR_GREEN : BLINK_COLOR_RED)
        : (now_ms() - t->pressed_ms >= 3ULL * t->pressed_unit_ms ? BLINK_COLOR_GREEN : BLINK_COLOR_RED);
    if (!t->pressed && *active) {
        ESP_ERROR_CHECK(stop_feedback(led));
        const uint64_t stopped = now_ms();
        if (t->morse.has_signal) morse_end(&t->morse, stopped);
        if (keyer->ready_at_ms) keyer->ready_at_ms = stopped + t->pressed_unit_ms;
    } else if (t->pressed && !*active) {
        ESP_ERROR_CHECK(blink_set_color(led, next_color));
        const esp_err_t ret = beep_set_active(true);
        if (ret != ESP_OK) {
            (void)blink_off(led);
            ESP_ERROR_CHECK(ret);
        }
        /* RMT/LEDC 启动耗时不侵占双键标准点划的持续时间。 */
        if (keyer->dual_mode) {
            t->pressed_ms = now_ms();
            keyer->signal_end_ms = t->pressed_ms + (keyer->symbol == '.' ? 1ULL : 3ULL) * t->pressed_unit_ms;
        }
    } else if (t->pressed && *color != next_color) {
        ESP_ERROR_CHECK(blink_set_color(led, next_color));
    }
    *active = t->pressed;
    *color = next_color;
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

static void show_state(display_handle_t screen, const keyer_t *keyer,
                       uint32_t selected, size_t page, const char label[4], display_frame_t *frame)
{
    const telegraph_t *telegraph = &keyer->telegraph;
    const morse_t *morse = &telegraph->morse;
    memset(frame, 0, sizeof(*frame));
    memcpy(frame->label, label, sizeof(frame->label));
    frame->selected_unit_ms = selected;
    frame->dual_mode = keyer->dual_mode;
    /* 复用解码器的七单位词边界，按住时即使旧词已结束也立即隐藏。 */
    frame->word_ready = !telegraph->pressed && !keyer->count && (!morse->has_signal || morse->word_done);
    frame->character_unit_ms = telegraph->pressed ? telegraph->pressed_unit_ms
                                                : (morse->current[0] ? morse->unit_ms : selected);
    strcpy(frame->current_code, morse->invalid ? "INVALID" : morse->current);
    frame->decoded = morse->current[0] ? (morse->invalid ? '?' : morse_decode(morse->current)) : ' ';
    frame->characters = morse->count;
    frame->pages = morse_history_lines(morse, page, DISPLAY_HISTORY_ROWS, DISPLAY_HISTORY_COLS, frame->history);
    frame->page = page + 1;
    ESP_ERROR_CHECK(display_show_frame(screen, frame));
}

void telegraph_app_run(void)
{
    /* 声光边沿优先于显示任务（优先级 2），避免短按在刷新期间积压成瞬时脉冲。 */
    vTaskPrioritySet(NULL, 3);
    const display_config_t screen_config = {
        .sclk_gpio = CONFIG_DISPLAY_SCLK_GPIO, .mosi_gpio = CONFIG_DISPLAY_MOSI_GPIO,
        .cs_gpio = CONFIG_DISPLAY_CS_GPIO, .dc_gpio = CONFIG_DISPLAY_DC_GPIO,
        .reset_gpio = CONFIG_DISPLAY_RST_GPIO,
    };
    /* 上电前统一检查引脚，避免任何外设初始化覆盖其他设备。 */
    const int pins[] = {CONFIG_BLINK_RGB_GPIO, CONFIG_BEEP_GPIO,
#ifdef CONFIG_INTERVAL_CONTROL_ENABLED
                        CONFIG_INTERVAL_CONTROL_ADC_GPIO,
#endif
                        CONFIG_BUTTON_INPUT_RED_GPIO, CONFIG_BUTTON_INPUT_BLACK_GPIO, CONFIG_BUTTON_INPUT_MODE_GPIO,
                        CONFIG_BUTTON_INPUT_GPIO, CONFIG_BUTTON_INPUT_BLUE_GPIO, screen_config.sclk_gpio,
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
#ifdef CONFIG_INTERVAL_CONTROL_ENABLED
    ESP_ERROR_CHECK(interval_control_start(CONFIG_INTERVAL_CONTROL_ADC_GPIO,
                                           CONFIG_INTERVAL_CONTROL_SAMPLE_INTERVAL_MS));
#else
    /* 未接电位器时不读取悬空引脚，沿用组件未启动的固定默认单位。 */
    ESP_LOGI(TAG, "Interval potentiometer disabled: fixed %u ms", INTERVAL_CONTROL_DEFAULT_UNIT_MS);
#endif
    QueueHandle_t events = xQueueCreate(64, sizeof(input_event_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    button_input_handle_t red_button = NULL, history_button = NULL, reset_button = NULL;
    ESP_ERROR_CHECK(button_input_create_edges(CONFIG_BUTTON_INPUT_RED_GPIO, on_red_edge,
                                              events, &red_button));
    button_input_handle_t black_button = NULL, mode_button = NULL;
    ESP_ERROR_CHECK(button_input_create_edges(CONFIG_BUTTON_INPUT_BLACK_GPIO, on_black_edge,
                                              events, &black_button));
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_MODE_GPIO, on_mode_press,
                                        events, &mode_button));
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_GPIO, on_history_press,
                                        events, &history_button));
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_BLUE_GPIO, on_reset_press,
                                        xTaskGetCurrentTaskHandle(), &reset_button));

    /* 上电与 esp_restart 均从双键模式启动，当前模式与切换目标保持一致。 */
    keyer_t keyer = {.dual_mode = true, .target_dual = true};
    telegraph_t *telegraph = &keyer.telegraph;
    display_frame_t frame;
    /* 每次启动生成一次标签；翻页、换档及模式切换均沿用同一标签。 */
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char label[4] = {0};
    for (size_t i = 0; i < sizeof(label) - 1; ++i) {
        label[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    /* 标签与本次启动同步，使用警告级别便于串口定位。 */
    ESP_LOGW(TAG, "label=%s", label);
    ESP_ERROR_CHECK(telegraph_sync_start(label));
    size_t page = 0;
    bool feedback_active = false;
    blink_color_t feedback_color = BLINK_COLOR_RED;
    bool wait_keys_release = false;
    uint32_t selected = interval_control_get_unit_ms();
    show_state(screen, &keyer, selected, page, label, &frame);
    ESP_LOGI(TAG, "Ready: S=single key, D=queued dot/dash; red=input, black=dash, gray=mode, green=page");
    while (true) {
        restart_if_requested(led);
        const size_t before = telegraph->morse.count;
        const bool last_word_end = before && telegraph->morse.history[before - 1].word_end;
        bool changed = false;
        unsigned page_requests = 0;
        if (atomic_exchange(&event_overflow, false)) {
            /* 丢失松开事件时取消未完成按键并等物理松开，等待松开后再接收下一次输入。 */
            ESP_ERROR_CHECK(stop_feedback(led));
            xQueueReset(events);
            keyer_cancel(&keyer);
            feedback_active = false;
            wait_keys_release = true;
            changed = true;
            ESP_LOGW(TAG, "Input event queue overflow: muted and canceled incomplete press");
        }
        if (wait_keys_release && gpio_get_level(CONFIG_BUTTON_INPUT_RED_GPIO) &&
            gpio_get_level(CONFIG_BUTTON_INPUT_BLACK_GPIO)) {
            wait_keys_release = false;
            memset(keyer.held, 0, sizeof(keyer.held));
            memset(keyer.blocked, 0, sizeof(keyer.blocked));
        }
        input_event_t event;
        /* 首个边沿直接唤醒输入任务；超时才检查静默与换档，不再固定延时轮询。
         * 先处理已采集边沿再结算静默，避免长按在三个单位临界处被拆分字符。 */
        const TickType_t wait = pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1;
        for (unsigned i = 0; i < 64; ++i) {
            if (xQueueReceive(events, &event, i == 0 ? wait : 0) != pdTRUE) break;
            if (event.kind == HISTORY_NEXT) {
                ++page_requests;
            } else if (event.kind == MODE_TOGGLE) {
                keyer_toggle_mode(&keyer);
                /* 点击时报告目标模式，发送中明确提示延后生效，避免误报当前状态。 */
                ESP_LOGI(TAG, "GPIO%d 模式切换：%s%s", CONFIG_BUTTON_INPUT_MODE_GPIO,
                         keyer.target_dual ? "双键模式（D）" : "单键模式（S）",
                         keyer.target_dual != keyer.dual_mode ? "，当前信号结束后生效" : "");
                changed = true;
            } else if (!wait_keys_release) {
                const keyer_key_t key = event.kind == RED_DOWN || event.kind == RED_UP ? KEYER_RED : KEYER_BLACK;
                const bool down = event.kind == RED_DOWN || event.kind == BLACK_DOWN;
                ESP_ERROR_CHECK(keyer_edge(&keyer, key, down, event.at_ms, event.selected_unit_ms)
                                 ? ESP_OK : ESP_ERR_NO_MEM);
                sync_feedback(&keyer, led, &feedback_active, &feedback_color);
                changed = true;
            }
        }
        const bool was_pressed = telegraph->pressed;
        const bool was_dual = keyer.dual_mode;
        ESP_ERROR_CHECK(keyer_advance(&keyer, now_ms(), selected) ? ESP_OK : ESP_ERR_NO_MEM);
        sync_feedback(&keyer, led, &feedback_active, &feedback_color);
        changed |= was_pressed != telegraph->pressed || was_dual != keyer.dual_mode;
        if (keyer.overflow) {
            /* 缓存满不静默丢键：停止发送并等待两键松开，串口明确报告。 */
            keyer.overflow = false;
            wait_keys_release = true;
            xQueueReset(events);
            ESP_LOGW(TAG, "Dual-key queue overflow: canceled pending signals; release both keys");
        }
        const size_t after = telegraph->morse.count;
        const bool next_word_end = after && telegraph->morse.history[after - 1].word_end;
        if (before != after || last_word_end != next_word_end) {
            /* 仅提交已确认历史快照；联网与重试由独立低优先级任务处理。 */
            ESP_ERROR_CHECK(telegraph_sync_history(&telegraph->morse));
            /* 新记录/词分组回到最新页；其余时间仅由绿键翻页，不再自动轮播。 */
            page = 0;
            changed = true;
            if (before != after) {
                const morse_entry_t *entry = &telegraph->morse.history[after - 1];
                ESP_LOGI(TAG, "History %u: %s = %c", (unsigned)after, entry->code, entry->decoded);
            }
        }
        if (page_requests) {
            const size_t pages = morse_history_lines(&telegraph->morse, 0, DISPLAY_HISTORY_ROWS,
                                                     DISPLAY_HISTORY_COLS, frame.history);
            page = (page + page_requests) % pages;
            changed = true;
        }
        const uint32_t next_selected = interval_control_get_unit_ms();
        changed |= selected != next_selected;
        selected = next_selected;
        if (changed) show_state(screen, &keyer, selected, page, label, &frame);
    }
}
