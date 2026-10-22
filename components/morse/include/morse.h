#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MORSE_MAX_ELEMENTS 8
#define MORSE_HISTORY_ROWS 10
#define MORSE_HISTORY_COLS 37

typedef struct {
    char code[MORSE_MAX_ELEMENTS + 1];
    char decoded;
    bool word_end;
} morse_entry_t;

typedef struct {
    char current[MORSE_MAX_ELEMENTS + 1];
    bool invalid;
    uint32_t unit_ms;
    uint64_t ended_ms;
    bool has_signal;
    bool character_done;
    bool word_done;
    morse_entry_t *history;
    size_t count;
    size_t capacity;
} morse_t;

char morse_decode(const char *code);
/* 所有时刻由调用方传入，使用实际发声结束时刻计算静默。 */
bool morse_advance(morse_t *morse, uint64_t now_ms);
void morse_begin(morse_t *morse, char symbol, uint32_t unit_ms);
void morse_end(morse_t *morse, uint64_t now_ms);
/* 最新字符/词组优先分页，词内顺序不变；行列数须在 ROWS/COLS 上限内且非零。 */
size_t morse_history_lines(const morse_t *morse, size_t page,
                         size_t row_count, size_t columns,
                         char rows[MORSE_HISTORY_ROWS][MORSE_HISTORY_COLS + 1]);
