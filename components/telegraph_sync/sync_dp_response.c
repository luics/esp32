#include "sync_dp_response.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#define DP_RESPONSE_LIMIT 16384
struct dp_response {
    char buffer[DP_RESPONSE_LIMIT + 1];
    char event[DP_RESPONSE_LIMIT + 1];
    size_t length, event_length;
    int mode; /* 0 未确定，1 JSON，2 SSE。 */
    bool failed;
    dp_content_fn callback;
    void *context;
};

static bool emit_json(dp_response_t *response, const char *json, bool stream)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    const cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    const cJSON *choice = cJSON_GetArrayItem(choices, 0);
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(choice, stream ? "delta" : "message");
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    if (cJSON_IsString(content) && content->valuestring && response->callback)
        response->callback(content->valuestring, response->context);
    cJSON_Delete(root);
    return true;
}

static bool finish_event(dp_response_t *response)
{
    if (!response->event_length) return true;
    response->event[response->event_length] = '\0';
    const bool ok = !strcmp(response->event, "[DONE]") || emit_json(response, response->event, true);
    response->event_length = 0;
    return ok;
}

static bool finish_line(dp_response_t *response)
{
    size_t n = response->length;
    if (n && response->buffer[n - 1] == '\r') --n;
    response->buffer[n] = '\0';
    if (!n) return finish_event(response);
    if (n >= 5 && !memcmp(response->buffer, "data:", 5)) {
        size_t start = 5;
        if (start < n && response->buffer[start] == ' ') ++start;
        const size_t add = n - start;
        if (response->event_length + add + (response->event_length != 0) > DP_RESPONSE_LIMIT) return false;
        if (response->event_length) response->event[response->event_length++] = '\n';
        memcpy(response->event + response->event_length, response->buffer + start, add);
        response->event_length += add;
    }
    return true;
}

dp_response_t *dp_response_create(dp_content_fn callback, void *context)
{
    dp_response_t *response = calloc(1, sizeof(*response));
    if (response) { response->callback = callback; response->context = context; }
    return response;
}

bool dp_response_feed(dp_response_t *response, const char *data, size_t length)
{
    if (!response || response->failed) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = data[i];
        if (!response->mode) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            response->mode = c == '{' || c == '[' ? 1 : 2;
        }
        if (response->mode == 2 && c == '\n') {
            if (!finish_line(response)) return !(response->failed = true);
            response->length = 0;
        } else {
            if (response->length == DP_RESPONSE_LIMIT) return !(response->failed = true);
            response->buffer[response->length++] = c;
        }
    }
    return true;
}

bool dp_response_finish(dp_response_t *response)
{
    if (!response || response->failed) return false;
    if (response->mode == 1) {
        response->buffer[response->length] = '\0';
        return emit_json(response, response->buffer, false);
    }
    if (response->mode == 2) {
        if (response->length && !finish_line(response)) return false;
        return finish_event(response);
    }
    return true;
}

void dp_response_free(dp_response_t *response) { free(response); }
