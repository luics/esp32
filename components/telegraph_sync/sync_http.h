#pragma once
#include "esp_err.h"

typedef enum { SYNC_HTTP_ACK, SYNC_HTTP_RETRY, SYNC_HTTP_BLOCK } sync_http_result_t;
/* 仅供同步任务串行调用；复用 HTTPS 连接。空 id 发 POST，已有 id 发 PATCH。 */
sync_http_result_t sync_http_send(char id[37], const char *json, int *status, esp_err_t *error);
/* 完整释放客户端与会话票据，仅在结束生命周期或状态无法恢复时使用。 */
void sync_http_reset(void);
/* 仅断开套接字，保留会话票据。 */
void sync_http_disconnect(void);

/* 联网校时后及空闲循环调用；首次预热、每 25 秒安全 HEAD 探测。 */
esp_err_t sync_http_prepare(void);
