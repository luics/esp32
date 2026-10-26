#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool dual_mode;
    bool motion;
    uint32_t unit_ms;
    char character[2];
    char code[10];
} telegraph_ha_state_t;

/* 启动一次；快照和模式命令均以单槽队列传递，输入任务不执行网络 I/O。 */
esp_err_t telegraph_ha_start(void);
void telegraph_ha_update(const telegraph_ha_state_t *state);
bool telegraph_ha_take_mode(bool *dual_mode);
