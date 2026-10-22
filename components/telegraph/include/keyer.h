#pragma once
#include "telegraph.h"

#define KEYER_QUEUE_SIZE 64
typedef enum { KEYER_RED, KEYER_BLACK } keyer_key_t;
typedef struct { char symbol; uint32_t unit_ms; } keyer_request_t;
typedef struct {
    telegraph_t telegraph;
    bool dual_mode;
    bool target_dual;
    bool held[2];
    bool blocked[2];
    bool overflow;
    char symbol;
    uint64_t signal_end_ms;
    uint64_t ready_at_ms;
    keyer_request_t queue[KEYER_QUEUE_SIZE];
    unsigned head, count;
} keyer_t;

/* 所有接口由输入任务串行调用；返回 false 仅表示历史分配失败。 */
bool keyer_edge(keyer_t *keyer, keyer_key_t key, bool down, uint64_t at_ms, uint32_t unit_ms);
bool keyer_advance(keyer_t *keyer, uint64_t now_ms, uint32_t selected_unit_ms);
/* 保留已完成输入，当前信号结束后切换，取消缓存及跨模式长按。 */
void keyer_toggle_mode(keyer_t *keyer);
void keyer_cancel(keyer_t *keyer);
