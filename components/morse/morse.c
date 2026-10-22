#include "morse.h"
#include <stdlib.h>
#include <string.h>

static const struct { char character; const char *code; } alphabet[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
    {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
    {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"},
    {'3', "...--"}, {'4', "....-"}, {'5', "....."}, {'6', "-...."},
    {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {':', "---..."},
    {'\'', ".----."}, {'-', "-....-"}, {'/', "-..-."}, {'(', "-.--."},
    {')', "-.--.-"}, {'"', ".-..-."}, {'=', "-...-"}, {'+', ".-.-."},
    {'@', ".--.-."},
};

char morse_decode(const char *code)
{
    for (size_t i = 0; i < sizeof(alphabet) / sizeof(alphabet[0]); ++i) {
        if (strcmp(code, alphabet[i].code) == 0) return alphabet[i].character;
    }
    return '?';
}

bool morse_advance(morse_t *morse, uint64_t now_ms)
{
    if (!morse->has_signal || now_ms < morse->ended_ms) return true;
    const uint64_t silence = now_ms - morse->ended_ms;
    /* 字符只提交一次；申请失败时保留输入，交由调用方明确报告。 */
    if (!morse->character_done && silence >= 3 * morse->unit_ms) {
        if (morse->count == morse->capacity) {
            const size_t capacity = morse->capacity ? morse->capacity * 2 : 128;
            morse_entry_t *history = realloc(morse->history, capacity * sizeof(*history));
            if (!history) return false;
            morse->history = history;
            morse->capacity = capacity;
        }
        morse_entry_t *entry = &morse->history[morse->count++];
        memset(entry, 0, sizeof(*entry));
        strcpy(entry->code, morse->current);
        entry->decoded = morse->invalid ? '?' : morse_decode(morse->current);
        if (morse->invalid) strcpy(entry->code, "INVALID");
        morse->current[0] = '\0';
        morse->invalid = false;
        morse->character_done = true;
    }
    /* 词间隔从同一次松开算起，不在字符间隔后再叠加七个单位。 */
    if (!morse->word_done && silence >= 7 * morse->unit_ms && morse->count) {
        morse->history[morse->count - 1].word_end = true;
        morse->word_done = true;
    }
    return true;
}

void morse_begin(morse_t *morse, char symbol, uint32_t unit_ms)
{
    if (!morse->current[0]) morse->unit_ms = unit_ms;
    const size_t length = strlen(morse->current);
    if (length < MORSE_MAX_ELEMENTS) {
        morse->current[length] = symbol;
        morse->current[length + 1] = '\0';
    } else {
        morse->invalid = true;
    }
    morse->character_done = false;
    morse->word_done = false;
}

void morse_end(morse_t *morse, uint64_t now_ms)
{
    morse->ended_ms = now_ms;
    morse->has_signal = true;
}

typedef struct {
    char (*rows)[MORSE_HISTORY_COLS + 1];
    size_t page;
    size_t row_count;
    size_t columns;
    size_t line;
} layout_t;

static void push_line(layout_t *layout, const char *line)
{
    if (layout->line / layout->row_count == layout->page) {
        strcpy(layout->rows[layout->line % layout->row_count], line);
    }
    ++layout->line;
}

static void append_text(layout_t *layout, char line[MORSE_HISTORY_COLS + 1], const char *text)
{
    for (; *text; ++text) {
        size_t length = strlen(line);
        if (length == layout->columns) {
            push_line(layout, line);
            line[0] = '\0';
            length = 0;
        }
        line[length] = *text;
        line[length + 1] = '\0';
    }
}

size_t morse_history_lines(const morse_t *morse, size_t page,
                           size_t row_count, size_t columns,
                           char rows[MORSE_HISTORY_ROWS][MORSE_HISTORY_COLS + 1])
{
    memset(rows, 0, MORSE_HISTORY_ROWS * sizeof(rows[0]));
    /* 屏幕尺寸只影响排版，不改变历史存储和字符/单词确认逻辑。 */
    if (!row_count || row_count > MORSE_HISTORY_ROWS || !columns || columns > MORSE_HISTORY_COLS) return 1;
    layout_t layout = {.rows = rows, .page = page, .row_count = row_count, .columns = columns};
    size_t cursor = morse->count;
    while (cursor) {
        const size_t end = cursor - 1;
        size_t start = end;
        /* 最新组优先；已确认单词内仍按原输入顺序展示，未成词逐字符倒序。 */
        if (morse->history[end].word_end) {
            while (start && !morse->history[start - 1].word_end) --start;
        }
        size_t code_length = end - start;
        for (size_t i = start; i <= end; ++i) code_length += strlen(morse->history[i].code);
        /* 每组紧凑显示为 "> 解码文本 摩斯码"，统一计算换行与分页占用。 */
        const size_t group_length = 3 + (end - start + 1) + code_length;
        const size_t group_lines = (group_length + columns - 1) / columns;
        const size_t used = layout.line % row_count;
        /* 一页容得下的单词不跨页；超长词才逐行延续。 */
        if (used && group_lines <= row_count && used + group_lines > row_count) {
            while (layout.line % row_count) push_line(&layout, "");
        }
        char line[MORSE_HISTORY_COLS + 1] = "";
        append_text(&layout, line, "> ");
        for (size_t i = start; i <= end; ++i) {
            const char decoded[] = {morse->history[i].decoded, '\0'};
            append_text(&layout, line, decoded);
        }
        append_text(&layout, line, " ");
        for (size_t i = start; i <= end; ++i) {
            if (i != start) append_text(&layout, line, " ");
            append_text(&layout, line, morse->history[i].code);
        }
        if (line[0]) push_line(&layout, line);
        cursor = start;
    }
    const size_t pages = (layout.line + row_count - 1) / row_count;
    return pages ? pages : 1;
}
