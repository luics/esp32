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
    assert(morse_history_lines(&m, 0, rows) == 1);
    assert(strcmp(rows[0], ".... . .-.. .-.. ---") == 0);
    assert(strcmp(rows[1], "> HELLO") == 0);
    assert(strcmp(rows[2], ".-- --- .-. .-.. -..") == 0);
    assert(strcmp(rows[3], "> WORLD") == 0);
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
    size_t pages = morse_history_lines(&m, 0, rows);
    assert(pages == 120); /* 未成词时每个字符占两行。 */
    size_t decoded = 0;
    for (size_t page = 0; page < pages; ++page) {
        assert(morse_history_lines(&m, page, rows) == pages);
        for (size_t row = 0; row < MORSE_HISTORY_ROWS; ++row) {
            for (const char *c = rows[row]; *c; ++c) decoded += *c == 'E';
        }
    }
    assert(decoded == 600);
    assert(morse_advance(&m, m.ended_ms + 420));
    pages = morse_history_lines(&m, 0, rows); /* 超长单词跨页，无截断。 */
    assert(pages > 1);
    decoded = 0;
    for (size_t page = 0; page < pages; ++page) {
        morse_history_lines(&m, page, rows);
        for (size_t row = 0; row < MORSE_HISTORY_ROWS; ++row) {
            for (const char *c = rows[row]; *c; ++c) decoded += *c == 'E';
        }
    }
    assert(decoded == 600);
    free(m.history);
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
    test_invalid_and_digits();
    puts("PASS: timing at all four speeds, HELLO WORLD, speed lock, invalid input, full-history pagination");
}
