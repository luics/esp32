#include "ha_protocol.h"
#include <string.h>

static bool equal(const char *data, size_t len, const char *text)
{
    return data && len == strlen(text) && !memcmp(data, text, len);
}

ha_message_t ha_receive(ha_rx_t *rx, const char *command_topic,
                       const char *topic, size_t topic_len, const char *data,
                       size_t len, size_t offset, size_t total, bool retained)
{
    if (!offset) {
        memset(rx, 0, sizeof(*rx));
        rx->total = total;
        rx->mode = equal(topic, topic_len, command_topic);
        rx->birth = equal(topic, topic_len, "homeassistant/status");
        rx->retained = retained;
    }
    /* 固定命令很短，整包校验后才投递，分片间不读取未知长度字符串。 */
    if (!total || total > sizeof(rx->payload) || total != rx->total ||
        offset != rx->received || offset > total || len > total - offset ||
        !data || (!rx->mode && !rx->birth)) {
        rx->mode = rx->birth = false;
        return HA_NONE;
    }
    memcpy(rx->payload + offset, data, len);
    rx->received += len;
    if (rx->received != total) return HA_NONE;
    const bool mode = rx->mode, birth = rx->birth, stale = rx->retained;
    rx->mode = rx->birth = false;
    if (mode && !stale && equal(rx->payload, total, "S")) return HA_SINGLE;
    if (mode && !stale && equal(rx->payload, total, "D")) return HA_DUAL;
    if (birth && equal(rx->payload, total, "online")) return HA_REDISCOVER;
    return HA_NONE;
}
