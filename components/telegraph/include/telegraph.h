#pragma once
#include "morse.h"

typedef struct {
    morse_t morse;
    bool pressed;
    uint64_t pressed_ms;
    uint32_t pressed_unit_ms;
} telegraph_t;

/* 按下先结算之前的静默，再锁定单位；按住期间不提交字符。 */
bool telegraph_press(telegraph_t *telegraph, uint64_t now_ms, uint32_t selected_unit_ms);
/* 松开即记录一个点/划（重复松开返回 0），不补播、不排队播放。
 * 传入边沿时刻用于识别；应用可在实际停声后用 morse_end 更新静默起点。 */
char telegraph_release(telegraph_t *telegraph, uint64_t now_ms);
bool telegraph_advance(telegraph_t *telegraph, uint64_t now_ms);
