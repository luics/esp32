#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef void (*dp_content_fn)(const char *content, void *context);
typedef struct dp_response dp_response_t;
dp_response_t *dp_response_create(dp_content_fn callback, void *context);
bool dp_response_feed(dp_response_t *response, const char *data, size_t length);
bool dp_response_finish(dp_response_t *response);
void dp_response_free(dp_response_t *response);
