#pragma once
#include <stdbool.h>
#include "morse.h"

typedef enum { DP_PROMPT_READY, DP_PROMPT_EMPTY, DP_PROMPT_TOO_LONG, DP_PROMPT_NO_MEMORY } dp_prompt_result_t;
/* 生成最新词的 JSON 快照；成功时调用方释放 *json。 */
dp_prompt_result_t dp_prompt_build(const morse_t *morse, char **json);
