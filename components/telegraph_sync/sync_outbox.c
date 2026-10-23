#include "sync_outbox.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *word_json(const morse_t *morse, size_t start, size_t end, const char *label)
{
    const size_t count = end - start;
    /* 字符最多需要反斜杠转义；每段摩斯码最多八字符及一个空格。 */
    if (count > (SIZE_MAX - 96) / (MORSE_MAX_ELEMENTS + 4)) return NULL;
    const size_t capacity = 96 + count * (MORSE_MAX_ELEMENTS + 4);
    char *json = malloc(capacity);
    if (!json) return NULL;
    char *p = json;
    p += snprintf(p, capacity, "{\"morse\":\"");
    for (size_t i = start; i < end; ++i) {
        if (i != start) *p++ = ' ';
        const size_t length = strlen(morse->history[i].code);
        memcpy(p, morse->history[i].code, length);
        p += length;
    }
    p += snprintf(p, capacity - (size_t)(p - json), "\",\"content\":\"");
    for (size_t i = start; i < end; ++i) {
        const char c = morse->history[i].decoded;
        if (c == '"' || c == '\\') *p++ = '\\';
        *p++ = c;
    }
    snprintf(p, capacity - (size_t)(p - json), "\",\"label\":\"%.3s\"}", label);
    return json;
}

bool sync_outbox_capture(sync_outbox_t *box, const morse_t *morse)
{
    while (box->consumed < morse->count) {
        const size_t i = box->consumed;
        const bool new_word = !i || morse->history[i - 1].word_end;
        sync_record_t *record = new_word ? calloc(1, sizeof(*record)) : box->tail;
        if (!record) return false;
        if (new_word) record->start = i;
        char *json = word_json(morse, record->start, i + 1, box->label);
        if (!json) {
            if (new_word) free(record);
            return false;
        }
        /* 在完整快照分配成功后才推进游标，失败可重试且不丢记录。 */
        free(record->json);
        record->json = json;
        ++record->revision;
        if (new_word) {
            /* 词边界仅确定下一条记录归属，不改变待上传的内容版本。 */
            if (box->tail && !box->tail->closed) {
                box->tail->closed = true;
            }
            if (box->tail) box->tail->next = record;
            else box->head = record;
            box->tail = record;
        }
        ++box->consumed;
    }
    /* 七单位确认仅关闭本地词分组，相同 JSON 不再触发 PATCH。 */
    if (box->tail && morse->count && morse->history[morse->count - 1].word_end && !box->tail->closed) {
        box->tail->closed = true;
    }
    return true;
}

void sync_record_ack(sync_record_t *record, size_t revision, const char id[37])
{
    memcpy(record->id, id, sizeof(record->id));
    /* 只确认请求携带的快照，网络请求期间新增字符仍保留待同步版本。 */
    record->sent_revision = revision;
}

bool sync_location_id(const char *location, const char *base_url, char id[37])
{
    const char *path = location;
    if (strncmp(path, base_url, strlen(base_url)) == 0) path += strlen(base_url);
    static const char prefix[] = "/api/telegraph/";
    if (strncmp(path, prefix, sizeof(prefix) - 1)) return false;
    const char *uuid = path + sizeof(prefix) - 1;
    if (strlen(uuid) != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        const char c = uuid[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    memcpy(id, uuid, 37);
    return true;
}

void sync_outbox_free(sync_outbox_t *box)
{
    while (box->head) {
        sync_record_t *record = box->head;
        box->head = record->next;
        free(record->json);
        free(record);
    }
    box->tail = NULL;
    box->consumed = 0;
}
