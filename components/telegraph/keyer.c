#include "keyer.h"

static void apply_mode(keyer_t *keyer)
{
    if (!keyer->telegraph.pressed && keyer->dual_mode != keyer->target_dual) {
        keyer->dual_mode = keyer->target_dual;
        keyer->ready_at_ms = 0;
    }
}

void keyer_cancel(keyer_t *keyer)
{
    /* 溢出时撤销未完成信号，保留已录入历史；物理松开后才能重新输入。 */
    keyer->head = keyer->count = 0;
    keyer->telegraph.pressed = false;
    keyer->ready_at_ms = 0;
    for (unsigned i = 0; i < 2; ++i) keyer->blocked[i] = true;
    apply_mode(keyer);
}

void keyer_toggle_mode(keyer_t *keyer)
{
    keyer->target_dual = !keyer->target_dual;
    keyer->head = keyer->count = 0;
    /* 不将切换前按住的键解释为新模式输入；单键按住仍正常等待松开。 */
    for (unsigned i = 0; i < 2; ++i) keyer->blocked[i] = keyer->held[i];
    apply_mode(keyer);
}

bool keyer_edge(keyer_t *keyer, keyer_key_t key, bool down, uint64_t at_ms, uint32_t unit_ms)
{
    if (!down) {
        keyer->held[key] = keyer->blocked[key] = false;
        if (!keyer->dual_mode && key == KEYER_RED) {
            (void)telegraph_release(&keyer->telegraph, at_ms);
            apply_mode(keyer);
        }
        return true;
    }
    if (keyer->held[key]) return true;
    keyer->held[key] = true;
    if (keyer->blocked[key]) return true;
    if (keyer->target_dual != keyer->dual_mode) {
        keyer->blocked[key] = true;
        return true;
    }
    if (!keyer->dual_mode) {
        return key == KEYER_BLACK || telegraph_press(&keyer->telegraph, at_ms, unit_ms);
    }
    /* 快速点击各占一个 FIFO 槽，松开不撤销已缓存的标准信号。 */
    if (keyer->count == KEYER_QUEUE_SIZE) {
        keyer->overflow = true;
        keyer_cancel(keyer);
        return true;
    }
    keyer->queue[(keyer->head + keyer->count++) % KEYER_QUEUE_SIZE] =
        (keyer_request_t){key == KEYER_RED ? '.' : '-', unit_ms};
    return true;
}

bool keyer_advance(keyer_t *keyer, uint64_t now_ms, uint32_t selected_unit_ms)
{
    telegraph_t *t = &keyer->telegraph;
    if (keyer->dual_mode && t->pressed && now_ms >= keyer->signal_end_ms) {
        /* 到期只结束一个信号；下次启动必须经过实际停声后的一个单位静默。 */
        (void)telegraph_release(t, keyer->signal_end_ms);
        morse_end(&t->morse, now_ms);
        keyer->ready_at_ms = now_ms + t->pressed_unit_ms;
        apply_mode(keyer);
        return true;
    }
    apply_mode(keyer);
    if (keyer->dual_mode && !t->pressed && now_ms >= keyer->ready_at_ms) {
        keyer_request_t request = {0};
        if (keyer->count) {
            request = keyer->queue[keyer->head];
            keyer->head = (keyer->head + 1) % KEYER_QUEUE_SIZE;
            --keyer->count;
        } else {
            const bool dot = keyer->held[KEYER_RED] && !keyer->blocked[KEYER_RED];
            const bool dash = keyer->held[KEYER_BLACK] && !keyer->blocked[KEYER_BLACK];
            /* 显式点击优先于自动重复；同时长按两键时交替，避免饿死其中一键。 */
            if (dot || dash) request = (keyer_request_t){
                dot && (!dash || keyer->symbol == '-') ? '.' : '-', selected_unit_ms,
            };
        }
        if (request.symbol) {
            if (!telegraph_press(t, now_ms, request.unit_ms)) return false;
            keyer->symbol = request.symbol;
            keyer->signal_end_ms = now_ms + (request.symbol == '.' ? 1ULL : 3ULL) * t->pressed_unit_ms;
        }
    }
    return telegraph_advance(t, now_ms);
}
