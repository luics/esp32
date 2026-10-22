#include "morse.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t send_code(morse_t *morse, const char *code, uint64_t start, uint32_t unit)
{
    for (size_t i = 0; code[i]; ++i) {
        assert(morse_advance(morse, start));
        morse_begin(morse, code[i], unit);
        start += (code[i] == '.' ? 1 : 3) * morse->unit_ms;
        morse_end(morse, start);
        if (code[i + 1]) start += morse->unit_ms;
    }
    return start;
}

static void test_boundaries_and_speed(void)
{
    const uint32_t units[] = {200, 150, 100, 60};
    for (size_t i = 0; i < 4; ++i) {
        const uint32_t unit = units[i];
        morse_t m = {0};
        uint64_t end = send_code(&m, ".", 0, unit);
        assert(m.count == 0 && strcmp(m.current, ".") == 0);
        assert(morse_advance(&m, end + unit * 3 - 1));
        assert(m.count == 0);
        assert(morse_advance(&m, end + unit * 3));
        assert(m.count == 1 && m.history[0].decoded == 'E' && !m.current[0]);
        assert(morse_advance(&m, end + unit * 7 - 1));
        assert(!m.history[0].word_end);
        assert(morse_advance(&m, end + unit * 7));
        assert(m.history[0].word_end);
        assert(morse_advance(&m, end + unit * 20));
        assert(m.count == 1);
        free(m.history);
    }
    morse_t m = {0};
    uint64_t end = send_code(&m, ".", 0, 200);
    morse_begin(&m, '-', 60);
    assert(m.unit_ms == 200); /* 旋钮改变不影响输入中的字符。 */
    morse_end(&m, end + 200 + 600);
    assert(morse_advance(&m, m.ended_ms + 600));
    assert(m.history[0].decoded == 'A');
    morse_begin(&m, '.', 60);
    assert(m.unit_ms == 60);
    free(m.history);
}

static void test_hello_world(void)
{
    const char *codes[] = {"....", ".", ".-..", ".-..", "---", ".--", "---", ".-.", ".-..", "-.."};
    const char *text = "HELLOWORLD";
    morse_t m = {0};
    uint64_t start = 0;
    for (size_t i = 0; i < 10; ++i) {
        uint64_t end = send_code(&m, codes[i], start, 150);
        /* 输入中的字符不出现在历史中。 */
        assert(m.count == i);
        start = end + (i == 4 ? 7 : 3) * 150;
        assert(morse_advance(&m, start));
        assert(m.count == i + 1 && m.history[i].decoded == text[i]);
        if (i == 4) assert(m.history[i].word_end);
        if (i == 9) assert(end == 16650);
    }
    assert(morse_advance(&m, m.ended_ms + 1050));
    char rows[MORSE_HISTORY_ROWS][MORSE_HISTORY_COLS + 1];
    assert(morse_history_lines(&m, 0, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows) == 1);
    assert(strcmp(rows[1], "> HELLO .... . .-.. .-.. ---") == 0);
    assert(strcmp(rows[0], "> WORLD .-- --- .-. .-.. -..") == 0);
    assert(rows[2][0] == '\0');
    free(m.history);
}

static void test_history_pagination(void)
{
    morse_t m = {0};
    uint64_t time = 0;
    for (size_t i = 0; i < 600; ++i) {
        time = send_code(&m, ".", time, 60) + 180;
        assert(morse_advance(&m, time));
    }
    assert(m.count == 600 && m.capacity >= 600);
    char rows[MORSE_HISTORY_ROWS][MORSE_HISTORY_COLS + 1];
    size_t pages = morse_history_lines(&m, 0, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows);
    assert(pages == 60); /* 紧凑格式每个字符只占一行。 */
    assert(strcmp(rows[0], "> E .") == 0);
    size_t decoded = 0;
    for (size_t page = 0; page < pages; ++page) {
        assert(morse_history_lines(&m, page, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows) == pages);
        for (size_t row = 0; row < MORSE_HISTORY_ROWS; ++row) {
            for (const char *c = rows[row]; *c; ++c) decoded += *c == 'E';
        }
    }
    assert(decoded == 600);
    assert(morse_advance(&m, m.ended_ms + 420));
    pages = morse_history_lines(&m, 0, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows); /* 超长单词跨页，无截断。 */
    assert(pages > 1);
    decoded = 0;
    for (size_t page = 0; page < pages; ++page) {
        morse_history_lines(&m, page, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows);
        for (size_t row = 0; row < MORSE_HISTORY_ROWS; ++row) {
            for (const char *c = rows[row]; *c; ++c) decoded += *c == 'E';
        }
    }
    assert(decoded == 600);
    free(m.history);
}

static void test_compact_word_wrapping(void)
{
    morse_entry_t entries[17] = {0};
    for (size_t i = 0; i < 17; ++i) {
        strcpy(entries[i].code, i < 8 ? "-----" : ".");
        entries[i].decoded = i < 8 ? '0' : 'E';
        entries[i].word_end = i == 7 || i >= 8;
    }
    morse_t m = {.history = entries, .count = 17};
    char rows[MORSE_HISTORY_ROWS][MORSE_HISTORY_COLS + 1];
    assert(morse_history_lines(&m, 0, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows) == 2);
    assert(strcmp(rows[8], "> E .") == 0);
    assert(rows[9][0] == '\0'); /* 两行单词整体移到下一页。 */
    assert(morse_history_lines(&m, 1, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows) == 2);
    char joined[2 * MORSE_HISTORY_COLS + 1];
    strcpy(joined, rows[0]);
    strcat(joined, rows[1]);
    assert(!strcmp(joined, "> 00000000 ----- ----- ----- ----- ----- ----- ----- -----"));
    assert(rows[2][0] == '\0');

    /* 五个数字的完整行恰好 37 列，不产生多余空行。 */
    m.history = entries;
    m.count = 5;
    m.history[4].word_end = true;
    assert(morse_history_lines(&m, 0, MORSE_HISTORY_ROWS, MORSE_HISTORY_COLS, rows) == 1);
    assert(!strcmp(rows[0], "> 00000 ----- ----- ----- ----- -----"));
    assert(strlen(rows[0]) == MORSE_HISTORY_COLS && rows[1][0] == '\0');
}

static void test_oled_pagination(void)
{
    /* 小屏词组整体移页，且每行都能在 128 像素视口内完整显示。 */
    morse_entry_t entries[600] = {0};
    for (size_t i = 0; i < 600; ++i) {
        strcpy(entries[i].code, ".");
        entries[i].decoded = 'E';
        entries[i].word_end = true;
    }
    morse_t m = {.history = entries, .count = 600};
    char rows[MORSE_HISTORY_ROWS][MORSE_HISTORY_COLS + 1];
    assert(morse_history_lines(&m, 0, 5, 20, rows) == 120);
    size_t seen = 0;
    for (size_t page = 0; page < 120; ++page) {
        assert(morse_history_lines(&m, page, 5, 20, rows) == 120);
        for (size_t row = 0; row < 5; ++row) {
            assert(!strcmp(rows[row], "> E ."));
            ++seen;
        }
        assert(!rows[5][0]);
    }
    assert(seen == 600);
    /* 实际 OLED 视口增加为六行后，全部记录仍恰好覆盖 100 页。 */
    assert(morse_history_lines(&m, 0, 6, 20, rows) == 100);
    seen = 0;
    for (size_t page = 0; page < 100; ++page) {
        assert(morse_history_lines(&m, page, 6, 20, rows) == 100);
        for (size_t row = 0; row < 6; ++row) {
            assert(!strcmp(rows[row], "> E ."));
            ++seen;
        }
        assert(!rows[6][0]);
    }
    assert(seen == 600);
    /* 第五组需要两行，完整移到下一页；延续行不丢字符。 */
    m.count = 7;
    for (size_t i = 0; i < 3; ++i) {
        strcpy(entries[i].code, "-----");
        entries[i].decoded = '0';
        entries[i].word_end = i == 2;
    }
    assert(morse_history_lines(&m, 0, 5, 20, rows) == 2);
    assert(!rows[4][0]);
    assert(morse_history_lines(&m, 1, 5, 20, rows) == 2);
    assert(strlen(rows[0]) == 20 && strlen(rows[1]) == 3);
    char joined[2 * MORSE_HISTORY_COLS + 1];
    strcpy(joined, rows[0]);
    strcat(joined, rows[1]);
    assert(!strcmp(joined, "> 000 ----- ----- -----"));
    /* 超长词跨越所有页，遍历后所有解码字符与点划均保留。 */
    m.count = 600;
    for (size_t i = 0; i < 600; ++i) {
        strcpy(entries[i].code, ".");
        entries[i].decoded = 'E';
        entries[i].word_end = i == 599;
    }
    size_t pages = morse_history_lines(&m, 0, 5, 20, rows);
    size_t decoded = 0, dots = 0;
    for (size_t page = 0; page < pages; ++page) {
        morse_history_lines(&m, page, 5, 20, rows);
        for (size_t row = 0; row < 5; ++row) {
            assert(strlen(rows[row]) <= 20);
            for (const char *c = rows[row]; *c; ++c) {
                decoded += *c == 'E';
                dots += *c == '.';
            }
        }
    }
    assert(decoded == 600 && dots == 600);
}

static void test_newest_first(void)
{
    morse_entry_t entries[] = {
        {.code = ".-", .decoded = 'A'},
        {.code = "-...", .decoded = 'B', .word_end = true},
        {.code = "-.-.", .decoded = 'C', .word_end = true},
        {.code = "-..", .decoded = 'D'},
        {.code = ".", .decoded = 'E'},
    };
    morse_t m = {.history = entries, .count = 5};
    char rows[MORSE_HISTORY_ROWS][MORSE_HISTORY_COLS + 1];
    assert(morse_history_lines(&m, 0, 6, 20, rows) == 1);
    assert(!strcmp(rows[0], "> E ."));
    assert(!strcmp(rows[1], "> D -.."));
    assert(!strcmp(rows[2], "> C -.-."));
    assert(!strcmp(rows[3], "> AB .- -..."));
    entries[4].word_end = true;
    assert(morse_history_lines(&m, 0, 1, 20, rows) == 3);
    assert(!strcmp(rows[0], "> DE -.. ."));
    morse_history_lines(&m, 1, 1, 20, rows);
    assert(!strcmp(rows[0], "> C -.-."));
    morse_history_lines(&m, 2, 1, 20, rows);
    assert(!strcmp(rows[0], "> AB .- -..."));
    /* 历史保持原时间顺序，倒序只发生在显示排版。 */
    assert(m.history[0].decoded == 'A' && m.history[4].decoded == 'E');
}

static void test_invalid_and_digits(void)
{
    assert(morse_decode("-----") == '0');
    assert(morse_decode(".----") == '1');
    assert(morse_decode("..---") == '2');
    assert(morse_decode("...--") == '3');
    assert(morse_decode("....-") == '4');
    assert(morse_decode(".....") == '5');
    assert(morse_decode("-....") == '6');
    assert(morse_decode("--...") == '7');
    assert(morse_decode("---..") == '8');
    assert(morse_decode("----.") == '9');
    assert(morse_decode("..--..") == '?');
    assert(morse_decode(".--.-.") == '@');
    morse_t m = {0};
    uint64_t end = send_code(&m, ".........", 0, 60);
    assert(m.invalid && m.count == 0);
    assert(morse_advance(&m, end + 180));
    assert(m.history[0].decoded == '?' && !strcmp(m.history[0].code, "INVALID"));
    assert(!m.invalid && !m.current[0]);
    free(m.history);
}

int main(void)
{
    test_boundaries_and_speed();
    test_hello_world();
    test_history_pagination();
    test_compact_word_wrapping();
    test_invalid_and_digits();
    test_newest_first();
    test_oled_pagination();
    puts("PASS: timing at all four speeds, HELLO WORLD, speed lock, invalid input, full-history pagination at 10x37, 5x20 and 6x20, newest-first groups");
}
