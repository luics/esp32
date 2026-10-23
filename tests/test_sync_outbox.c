#include "sync_outbox.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    sync_outbox_t box = {.label = "A09"};
    morse_t morse = {0};
    assert(sync_outbox_capture(&box, &morse));
    assert(!box.head);
    morse_begin(&morse, '.', 100);
    morse_end(&morse, 100);
    assert(morse_advance(&morse, 399));
    assert(sync_outbox_capture(&box, &morse));
    assert(!box.head); /* 输入中的字符不可上传。 */
    assert(morse_advance(&morse, 400));
    assert(sync_outbox_capture(&box, &morse));
    sync_record_t *first = box.head;
    assert(first && !first->next && !first->id[0] && first->revision == 1);
    assert(!strcmp(first->json, "{\"morse\":\".\",\"content\":\"E\",\"label\":\"A09\"}"));
    strcpy(first->id, "01234567-89ab-cdef-0123-456789abcdef");
    first->sent_revision = first->revision;
    morse_begin(&morse, '-', 100);
    morse_end(&morse, 500);
    assert(morse_advance(&morse, 800));
    assert(sync_outbox_capture(&box, &morse));
    assert(box.head == first && box.tail == first && !first->next);
    assert(first->revision == 2 && first->sent_revision == 1 && first->id[0]);
    assert(!strcmp(first->json, "{\"morse\":\". -\",\"content\":\"ET\",\"label\":\"A09\"}"));
    assert(morse_advance(&morse, 1200));
    assert(sync_outbox_capture(&box, &morse));
    assert(first->revision == 2 && first->closed); /* 词确认不改变上传字段。 */
    assert(sync_outbox_capture(&box, &morse));
    assert(first->revision == 2); /* 重复快照不重复提交。 */
    const char uuid[] = "01234567-89ab-cdef-0123-456789abcdef";
    sync_record_ack(first, 1, uuid);
    assert(first->sent_revision == 1 && first->revision == 2); /* POST 期间新增字符仍需 PATCH。 */
    sync_record_ack(first, 2, uuid);
    assert(first->sent_revision == first->revision);
    /* 预连接期间单字符词已确认：POST 最新内容后不补相同 PATCH。 */
    morse_entry_t single = {.code=".", .decoded='E', .word_end=true};
    morse_t single_history = {.history=&single, .count=1};
    sync_outbox_t delayed = {.label="A09"};
    assert(sync_outbox_capture(&delayed, &single_history));
    assert(delayed.head->closed && delayed.head->revision==1);
    sync_record_ack(delayed.head, 1, uuid);
    assert(delayed.head->sent_revision==delayed.head->revision);
    assert(sync_outbox_capture(&delayed, &single_history));
    assert(delayed.head->sent_revision==delayed.head->revision);
    sync_outbox_free(&delayed);
    morse_begin(&morse, '.', 100);
    morse_end(&morse, 1500);
    assert(morse_advance(&morse, 1800));
    assert(sync_outbox_capture(&box, &morse));
    assert(box.tail != first && box.tail == first->next && !box.tail->id[0]);
    assert(!strcmp(box.tail->json, "{\"morse\":\".\",\"content\":\"E\",\"label\":\"A09\"}"));
    /* 多条积压历史仍按词分组，JSON 正确转义引号，未知编码保留问号。 */
    morse_entry_t entries[] = {
        {.code=".-..-.", .decoded='"'}, {.code="........", .decoded='?', .word_end=true},
        {.code="INVALID", .decoded='?', .word_end=true},
    };
    sync_outbox_t other = {.label="XYZ"};
    morse_t backlog = {.history=entries, .count=3};
    assert(sync_outbox_capture(&other, &backlog));
    assert(!strcmp(other.head->json, "{\"morse\":\".-..-. ........\",\"content\":\"\\\"?\",\"label\":\"XYZ\"}"));
    assert(other.head->next == other.tail && other.tail->start == 2);
    char id[37];
    const char *base = "https://telegraph.bryanxu.top";
    assert(sync_location_id("/api/telegraph/01234567-89ab-cdef-0123-456789abcdef", base, id));
    assert(!strcmp(id, first->id));
    assert(sync_location_id("https://telegraph.bryanxu.top/api/telegraph/01234567-89ab-cdef-0123-456789abcdef", base, id));
    assert(!sync_location_id("https://evil.example/api/telegraph/01234567-89ab-cdef-0123-456789abcdef", base, id));
    assert(!sync_location_id("/api/telegraph/not-a-uuid", base, id));
    assert(!sync_location_id("/api/telegraph/01234567-89ab-cdef-0123-456789abcdef/extra", base, id));
    assert(!sync_location_id("/api/telegraph/01234567-89ab-cdef-0123-456789abcdeg", base, id));
    sync_outbox_free(&box);
    sync_outbox_free(&other);
    free(morse.history);
    puts("PASS: confirmed history only, per-word POST identity, growing-word PATCH identity, offline backlog, JSON escaping, Location UUID validation");
}
