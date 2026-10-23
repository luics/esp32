#pragma once
#include "morse.h"

typedef struct sync_record {
    struct sync_record *next;
    size_t start, revision, sent_revision;
    char id[37];
    char *json;
    bool blocked;
    bool closed;
} sync_record_t;

typedef struct {
    sync_record_t *head, *tail;
    size_t consumed;
    char label[4];
} sync_outbox_t;

/* 调用方串行访问；按词边界保存待同步的最新完整内容。 */
bool sync_outbox_capture(sync_outbox_t *box, const morse_t *morse);
/* 只确认已发送的版本，保留请求期间新增字符的待同步版本。 */
void sync_record_ack(sync_record_t *record, size_t revision, const char id[37]);
/* 从 Location 提取 UUID，只接受本接口的相对路径或同源绝对 URL。 */
bool sync_location_id(const char *location, const char *base_url, char id[37]);
void sync_outbox_free(sync_outbox_t *box);
