#include "telegraph_app.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include <string.h>
#include "blink.h"
#include "beep.h"
#include "display.h"
#include "button_input.h"
#include "interval_control.h"
#include "pir_sensor.h"
#include "morse.h"
#include "keyer.h"
#include "telegraph_sync.h"
#include "dp_prompt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#ifdef CONFIG_TELEGRAPH_HA_ENABLED
#include "telegraph_ha.h"
#endif

static const char *TAG = "telegraph";
static atomic_bool reset_armed;
static atomic_bool event_overflow;
typedef struct { char lines[2][21]; bool active; } dp_preview_t;

static void on_dp_content(bool start, const char *content, void *context)
{
    static dp_preview_t preview;
    static unsigned row, column;
    if (start) {
        memset(&preview, 0, sizeof(preview));
        preview.active = true;
        row = column = 0;
    } else if (content) {
        /* 预览保留末两行；不支持的 UTF-8 字符以一个问号表示。 */
        for (const unsigned char *p = (const unsigned char *)content; *p; ++p) {
            if ((*p & 0xc0) == 0x80) continue;
            if (*p == '\r') continue;
            if (column == 20 || *p == '\n') {
                if (row == 0) row = 1;
                else { memcpy(preview.lines[0], preview.lines[1], 21); memset(preview.lines[1], 0, 21); }
                column = 0;
                if (*p == '\n') continue;
            }
            preview.lines[row][column++] = *p >= 32 && *p < 127 ? (char)*p : '?';
        }
    }
    xQueueOverwrite((QueueHandle_t)context, &preview);
}
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

static void on_mode_click(void *context)
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
        /* 只记录实际点亮与切色，避免持续按住时重复刷日志。 */
        ESP_LOGI(TAG, "板载 LED GPIO%d 触发：%s（%s）", CONFIG_BLINK_RGB_GPIO,
                 next_color == BLINK_COLOR_RED ? "红光" : "绿光",
                 keyer->dual_mode ? (keyer->symbol == '.' ? "双键点" : "双键划") : "单键输入");
    } else if (t->pressed && *color != next_color) {
        ESP_ERROR_CHECK(blink_set_color(led, next_color));
        ESP_LOGI(TAG, "板载 LED GPIO%d 触发：绿光（单键长按达到划阈值）", CONFIG_BLINK_RGB_GPIO);
    }
    *active = t->pressed;
    *color = next_color;
}

typedef struct {
    bool active;
    uint8_t red, green, blue;
    uint64_t mode_until_ms;
} external_feedback_t;

static unsigned pir_feedback_level(uint32_t active_ms)
{
    const unsigned level = active_ms / 1000;
    return level < 3 ? level : 3;
}

static void sync_external_feedback(blink_handle_t led, bool signal_active,
                                   blink_color_t signal_color, const pir_sensor_sample_t *motion,
                                   external_feedback_t *state)
{
    /* 模式黄光优先，随后是点划发送；空闲时按 PIR 持续时间显示四档闪光。 */
    const bool mode_active = now_ms() < state->mode_until_ms;
    uint8_t red = 0, green = 0, blue = 0;
    bool active = false;
    if (mode_active) {
        active = true;
        red = green = 64;
    } else if (signal_active) {
        active = true;
        red = signal_color == BLINK_COLOR_RED ? 64 : 0;
        green = signal_color == BLINK_COLOR_GREEN ? 64 : 0;
    } else if (motion->motion) {
        /* PIR 仅输出高低电平，用高电平持续时间表示活动档位。 */
        static const uint8_t colors[4][3] = {
            {64, 64, 64}, {0, 64, 0}, {64, 64, 0}, {64, 0, 0},
        };
        static const uint16_t periods_ms[4] = {500, 400, 300, 150};
        const unsigned level = pir_feedback_level(motion->active_ms);
        const uint16_t period = periods_ms[level];
        active = motion->active_ms % period < period / 2;
        red = colors[level][0];
        green = colors[level][1];
        blue = colors[level][2];
    }
    if (active && (!state->active || state->red != red || state->green != green || state->blue != blue)) {
        ESP_ERROR_CHECK(blink_set_rgb(led, red, green, blue));
    } else if (!active && state->active) {
        ESP_ERROR_CHECK(blink_off(led));
    }
    state->active = active;
    state->red = red;
    state->green = green;
    state->blue = blue;
}

static void restart_if_requested(blink_handle_t led, blink_handle_t external_led)
{
    /* 上电时仍按住蓝键则忽略，松开后才接受下一次按下，防止循环重启。 */
    if (gpio_get_level(CONFIG_BUTTON_INPUT_BLUE_GPIO)) atomic_store(&reset_armed, true);
    if (ulTaskNotifyTake(pdTRUE, 0) == 0) return;
    ESP_LOGI(TAG, "Blue reset: restarting through app_main initialization");
    (void)stop_feedback(led);
    (void)blink_off(external_led);
    /* 统一复用现有启动流程，避免单独维护一份状态与外设重置逻辑。 */
    esp_restart();
}

static void post_latest_word(const morse_t *morse)
{
    /* 构造当前词的快照后入队，后续输入不会修改已提交的请求。 */
    char *prompt = NULL;
    const dp_prompt_result_t result = dp_prompt_build(morse, &prompt);
    if (result == DP_PROMPT_READY) {
        const esp_err_t err = telegraph_dp_post(prompt);
        if (err != ESP_OK) ESP_LOGW(TAG, "DeepSeek request not queued: %s", esp_err_to_name(err));
        free(prompt);
    } else {
        ESP_LOGW(TAG, "DeepSeek request skipped: word unavailable (%d)", result);
    }
}

static void show_state(display_handle_t screen, const keyer_t *keyer,
                       uint32_t selected, size_t page, const char label[4], const dp_preview_t *preview,
                       display_frame_t *frame)
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
    frame->dp_preview_active = preview->active && !telegraph->pressed && !morse->current[0] && !keyer->count;
    memcpy(frame->dp_preview, preview->lines, sizeof(frame->dp_preview));
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
    const int pins[] = {CONFIG_BLINK_RGB_GPIO, CONFIG_BLINK_MODE_GPIO, CONFIG_PIR_SENSOR_GPIO,
                        CONFIG_BEEP_GPIO,
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
    blink_handle_t led = NULL, external_led = NULL;
    pir_sensor_handle_t pir = NULL;
    ESP_ERROR_CHECK(display_init(&screen_config, &screen));
    ESP_ERROR_CHECK(blink_create(CONFIG_BLINK_RGB_GPIO, &led));
    /* 外接标准 WS2812 独立使用 GRB，不继承实板板载灯的 RGB 顺序。 */
    ESP_ERROR_CHECK(blink_create_with_order(CONFIG_BLINK_MODE_GPIO, BLINK_ORDER_GRB, &external_led));
    ESP_ERROR_CHECK(pir_sensor_create(CONFIG_PIR_SENSOR_GPIO, &pir));
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
    QueueHandle_t dp_updates = xQueueCreate(1, sizeof(dp_preview_t));
    ESP_ERROR_CHECK(dp_updates ? ESP_OK : ESP_ERR_NO_MEM);
    telegraph_dp_set_content_callback(on_dp_content, dp_updates);
    button_input_handle_t red_button = NULL, history_button = NULL, reset_button = NULL;
    ESP_ERROR_CHECK(button_input_create_edges(CONFIG_BUTTON_INPUT_RED_GPIO, on_red_edge,
                                              events, &red_button));
    button_input_handle_t black_button = NULL, mode_button = NULL;
    ESP_ERROR_CHECK(button_input_create_edges(CONFIG_BUTTON_INPUT_BLACK_GPIO, on_black_edge,
                                              events, &black_button));
    ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_MODE_GPIO, on_mode_click,
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
#ifdef CONFIG_TELEGRAPH_HA_ENABLED
    /* MQTT 启动失败不影响本地输入；网络 I/O 留在低优先级任务。 */
    const esp_err_t ha_err = telegraph_ha_start();
    if (ha_err != ESP_OK) ESP_LOGE(TAG, "HA disabled: %s", esp_err_to_name(ha_err));
#endif
    size_t page = 0;
    bool feedback_active = false;
    blink_color_t feedback_color = BLINK_COLOR_RED;
    external_feedback_t external_feedback = {0};
    pir_sensor_sample_t motion = {0};
    int motion_level = -1;
    bool wait_keys_release = false;
    unsigned pending_dp_requests = 0;
    uint32_t selected = interval_control_get_unit_ms();
    dp_preview_t dp_preview = {0};
    show_state(screen, &keyer, selected, page, label, &dp_preview, &frame);
    ESP_LOGI(TAG, "Ready: S=single key, D=queued dot/dash; dot=input, dash=dash, yellow=mode, BOOT=page");
    ESP_LOGI(TAG, "LEDs: dot/dash GPIO%d, external GPIO%d (dot/dash + mode + PIR GPIO%d)",
             CONFIG_BLINK_RGB_GPIO, CONFIG_BLINK_MODE_GPIO, CONFIG_PIR_SENSOR_GPIO);
    while (true) {
        restart_if_requested(led, external_led);
        const size_t before = telegraph->morse.count;
        const bool last_word_end = before && telegraph->morse.history[before - 1].word_end;
        bool changed = false;
        if (xQueueReceive(dp_updates, &dp_preview, 0) == pdTRUE) changed = true;
        unsigned page_requests = 0;
        if (atomic_exchange(&event_overflow, false)) {
            /* 丢失松开事件时取消未完成按键并等物理松开，等待松开后再接收下一次输入。 */
            ESP_ERROR_CHECK(stop_feedback(led));
            xQueueReset(events);
            keyer_cancel(&keyer);
            feedback_active = false;
            sync_external_feedback(external_led, false, feedback_color, &motion, &external_feedback);
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
                /* 发送中的点划先完成，再采集词快照，避免截断末尾字符。 */
                if (telegraph->pressed || keyer.count) {
                    if (pending_dp_requests < 4) ++pending_dp_requests;
                    else ESP_LOGW(TAG, "DeepSeek pending requests full");
                } else post_latest_word(&telegraph->morse);
                keyer_toggle_mode(&keyer);
                /* 重复点击重新计时并记录；黄光优先期间仍继续发送点划。 */
                external_feedback.mode_until_ms = now_ms() + 200;
                external_feedback.active = false;
                sync_external_feedback(external_led, feedback_active, feedback_color, &motion, &external_feedback);
                ESP_LOGI(TAG, "外接 LED GPIO%d：模式黄光 200 ms", CONFIG_BLINK_MODE_GPIO);
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
                sync_external_feedback(external_led, feedback_active, feedback_color, &motion, &external_feedback);
                changed = true;
            }
        }
#ifdef CONFIG_TELEGRAPH_HA_ENABLED
        bool requested_dual;
        if (telegraph_ha_take_mode(&requested_dual) && requested_dual != keyer.target_dual) {
            /* 复用原状态机的延迟切换及跨模式松开保护，只上报实际模式。 */
            keyer_toggle_mode(&keyer);
            external_feedback.mode_until_ms = now_ms() + 200;
            changed = true;
        }
#endif
        const bool was_pressed = telegraph->pressed;
        const bool was_dual = keyer.dual_mode;
        ESP_ERROR_CHECK(keyer_advance(&keyer, now_ms(), selected) ? ESP_OK : ESP_ERR_NO_MEM);
        sync_feedback(&keyer, led, &feedback_active, &feedback_color);
        if (pending_dp_requests && !telegraph->pressed && !keyer.count) {
            /* 每轮只入队一次，避免连续点击影响声光任务的响应。 */
            post_latest_word(&telegraph->morse);
            --pending_dp_requests;
        }
        ESP_ERROR_CHECK(pir_sensor_sample(pir, now_ms(), &motion));
        const int next_motion_level = motion.motion ? (int)pir_feedback_level(motion.active_ms) : -1;
        if (next_motion_level != motion_level && next_motion_level >= 0) {
            static const char *const colors[] = {"white", "green", "yellow", "red"};
            ESP_LOGI(TAG, "PIR activity level %d/4: GPIO%d LED %s blink", next_motion_level + 1,
                     CONFIG_BLINK_MODE_GPIO, colors[next_motion_level]);
        }
        motion_level = next_motion_level;
        sync_external_feedback(external_led, feedback_active, feedback_color, &motion, &external_feedback);
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
#ifdef CONFIG_TELEGRAPH_HA_ENABLED
        telegraph_ha_state_t ha_state = {
            .dual_mode = keyer.dual_mode, .motion = motion.motion,
            .unit_ms = telegraph->pressed ? telegraph->pressed_unit_ms
                       : (telegraph->morse.current[0] ? telegraph->morse.unit_ms : selected),
        };
        if (after) {
            const morse_entry_t *last = &telegraph->morse.history[after - 1];
            ha_state.character[0] = last->decoded;
            snprintf(ha_state.code, sizeof(ha_state.code), "%s", last->code);
        }
        telegraph_ha_update(&ha_state);
#endif
        if (changed) show_state(screen, &keyer, selected, page, label, &dp_preview, &frame);
    }
}
