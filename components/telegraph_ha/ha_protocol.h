#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { HA_NONE, HA_SINGLE, HA_DUAL, HA_REDISCOVER } ha_message_t;
typedef struct {
    size_t received, total;
    bool mode, birth, retained;
    char payload[6];
} ha_rx_t;
/* 有界重组；拒绝超长、错位分片和 retained 控制，主题/数据均不要求 NUL。 */
ha_message_t ha_receive(ha_rx_t *rx, const char *command_topic,
                       const char *topic, size_t topic_len, const char *data,
                       size_t len, size_t offset, size_t total, bool retained);
