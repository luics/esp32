#include "dp_prompt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *json = NULL;
    morse_t morse = {0};
    assert(dp_prompt_build(&morse, &json) == DP_PROMPT_EMPTY && !json);
    morse_entry_t entries[] = {
        {.decoded = 'O'}, {.decoded = 'L'}, {.decoded = 'D', .word_end = true},
        {.decoded = 'H'}, {.decoded = 'I'},
    };
    morse.history = entries;
    morse.count = 5;
    assert(dp_prompt_build(&morse, &json) == DP_PROMPT_READY);
    assert(strstr(json, "\"content\":\"HI\"") && strstr(json, "\"reasoning_effort\":\"none\""));
    free(json);
    entries[4].word_end = true;
    assert(dp_prompt_build(&morse, &json) == DP_PROMPT_READY);
    assert(strstr(json, "\"content\":\"HI\""));
    free(json);
    entries[4].word_end = false;
    strcpy(morse.current, ".");
    assert(dp_prompt_build(&morse, &json) == DP_PROMPT_READY);
    assert(strstr(json, "\"content\":\"HIE\""));
    free(json);
    entries[4].word_end = true;
    strcpy(morse.current, ".-..-."); /* 双引号需 JSON 转义。 */
    assert(dp_prompt_build(&morse, &json) == DP_PROMPT_READY);
    assert(strstr(json, "\"content\":\"\\\"\""));
    free(json);
    morse.invalid = true;
    assert(dp_prompt_build(&morse, &json) == DP_PROMPT_READY);
    assert(strstr(json, "\"content\":\"?\""));
    free(json);
    morse_entry_t *large = calloc(4096, sizeof(*large));
    assert(large);
    for (size_t i = 0; i < 4096; ++i) large[i].decoded = 'A';
    morse = (morse_t){.history = large, .count = 4096};
    assert(dp_prompt_build(&morse, &json) == DP_PROMPT_TOO_LONG && !json);
    free(large);
    puts("PASS: latest word, pending candidate, word boundary, JSON escaping and size limit");
}
