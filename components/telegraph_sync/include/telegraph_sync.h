#pragma once
#include "esp_err.h"
#include "morse.h"

/* 启动低优先级联网任务；未配置 SSID 时保留离线电报机功能。 */
esp_err_t telegraph_sync_start(const char label[4]);
/* 只复制已确认历史，不执行网络 I/O；内存不足明确返回错误。 */
esp_err_t telegraph_sync_history(const morse_t *morse);

/* 共享 Wi-Fi 的 IP 就绪状态；不重复初始化网络。 */
bool telegraph_sync_network_ready(void);
