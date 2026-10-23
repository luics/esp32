#pragma once
#include "esp_err.h"

/* 同步任务串行调用；每次实际建联均输出计时，复用连接的请求不重复输出。 */
void sync_tls_timing_begin(void);
void sync_tls_timing_configured(const void *config);
void sync_tls_timing_connected(void);
void sync_tls_timing_finish(esp_err_t error);
