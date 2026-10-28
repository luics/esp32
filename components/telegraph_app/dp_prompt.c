#include "dp_prompt.h"
#include <stdlib.h>
#include <string.h>

#define DP_JSON_MAX 4096

dp_prompt_result_t dp_prompt_build(const morse_t *morse, char **json)
{
    static const char prefix[] = "{\"messages\":[{\"role\":\"user\",\"content\":\"";
    static const char suffix[] = "\"}],\"stream\":true,\"reasoning_effort\":\"none\"}";
    *json = NULL;
    const bool candidate = morse->current[0] != '\0';
    size_t start = morse->count;
    if (start && (!candidate || !morse->history[start - 1].word_end)) {
        --start;
        while (start && !morse->history[start - 1].word_end) --start;
    }
    const size_t count = morse->count - start;
    if (!count && !candidate) return DP_PROMPT_EMPTY;

    /* 精确计算 JSON 长度，拒绝过长词而不是截断；引号和反斜杠必须转义。 */
    size_t length = sizeof(prefix) - 1 + sizeof(suffix) - 1;
    for (size_t i = 0; i < count + (size_t)candidate; ++i) {
        const char c = i < count ? morse->history[start + i].decoded :
                       (morse->invalid ? '?' : morse_decode(morse->current));
        length += (c == '"' || c == '\\') ? 2 : 1;
        if (length > DP_JSON_MAX) return DP_PROMPT_TOO_LONG;
    }
    char *buffer = malloc(length + 1);
    if (!buffer) return DP_PROMPT_NO_MEMORY;
    char *p = buffer;
    memcpy(p, prefix, sizeof(prefix) - 1); p += sizeof(prefix) - 1;
    for (size_t i = 0; i < count + (size_t)candidate; ++i) {
        const char c = i < count ? morse->history[start + i].decoded :
                       (morse->invalid ? '?' : morse_decode(morse->current));
        if (c == '"' || c == '\\') *p++ = '\\';
        *p++ = c;
    }
    memcpy(p, suffix, sizeof(suffix));
    *json = buffer;
    return DP_PROMPT_READY;
}
