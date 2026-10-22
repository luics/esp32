#include "keyer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void tap(keyer_t *k, keyer_key_t key, uint64_t at, uint32_t unit)
{
    assert(keyer_edge(k, key, true, at, unit));
    assert(keyer_edge(k, key, false, at + 1, unit));
}

static void test_four_speeds(void)
{
    const uint32_t units[] = {200, 150, 100, 60};
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t u = units[i];
        keyer_t k = {0};
        keyer_toggle_mode(&k);
        tap(&k, KEYER_RED, 0, u);
        tap(&k, KEYER_BLACK, 2, 60); /* 下一个缓存信号沿用当前字符的速度。 */
        assert(keyer_advance(&k, 5, u) && k.telegraph.pressed);
        assert(keyer_advance(&k, 5 + u - 1, u) && k.telegraph.pressed);
        assert(keyer_advance(&k, 5 + u, u) && !k.telegraph.pressed);
        assert(!strcmp(k.telegraph.morse.current, "."));
        assert(keyer_advance(&k, 5 + 2*u - 1, u) && !k.telegraph.pressed);
        assert(keyer_advance(&k, 5 + 2*u, u) && k.telegraph.pressed);
        assert(keyer_advance(&k, 5 + 5*u - 1, u) && k.telegraph.pressed);
        assert(keyer_advance(&k, 5 + 5*u, u) && !k.telegraph.pressed);
        assert(!strcmp(k.telegraph.morse.current, ".-"));
        assert(keyer_advance(&k, 5 + 12*u - 1, u) && !k.telegraph.morse.word_done);
        assert(keyer_advance(&k, 5 + 12*u, u) && k.telegraph.morse.word_done);
        assert(k.telegraph.morse.history[0].decoded == 'A');
        free(k.telegraph.morse.history);
    }
}

static void test_switch_and_overflow(void)
{
    keyer_t k = {0};
    assert(keyer_edge(&k, KEYER_RED, true, 0, 100));
    keyer_toggle_mode(&k); /* 单键松开后才完成切换，时长识别仍遵循旧模式。 */
    assert(!k.dual_mode);
    assert(keyer_edge(&k, KEYER_RED, false, 300, 60));
    assert(k.dual_mode && !strcmp(k.telegraph.morse.current, "-"));
    assert(keyer_advance(&k, 1000, 60));
    assert(k.telegraph.morse.count == 1);
    for (unsigned i = 0; i < KEYER_QUEUE_SIZE; ++i) tap(&k, i%2, 1001+2*i, 60);
    assert(k.count == KEYER_QUEUE_SIZE);
    assert(keyer_edge(&k, KEYER_RED, true, 1200, 60));
    assert(k.overflow && !k.count && !k.telegraph.pressed && k.telegraph.morse.count == 1);
    assert(keyer_advance(&k, 1300, 60) && !k.telegraph.pressed);
    assert(keyer_edge(&k, KEYER_RED, false, 1400, 60));
    tap(&k, KEYER_RED, 1500, 60);
    assert(keyer_advance(&k, 1505, 60) && k.telegraph.pressed);
    keyer_toggle_mode(&k);
    assert(k.dual_mode); /* 标准点不可在模式切换时被截断。 */
    assert(keyer_advance(&k, 1565, 60) && !k.dual_mode && !k.telegraph.pressed);
    free(k.telegraph.morse.history);
}

static void test_both_held_and_no_catchup(void)
{
    keyer_t k = {0};
    keyer_toggle_mode(&k);
    assert(keyer_edge(&k, KEYER_RED, true, 0, 100));
    assert(keyer_edge(&k, KEYER_BLACK, true, 0, 100));
    assert(keyer_advance(&k, 0, 100) && k.symbol == '.');
    /* 调度延迟时只结束一个信号，不压缩静默或瞬时补播多个点划。 */
    assert(keyer_advance(&k, 1000, 100) && !k.telegraph.pressed);
    assert(keyer_advance(&k, 1099, 100) && !k.telegraph.pressed);
    assert(keyer_advance(&k, 1100, 100) && k.symbol == '-');
    assert(keyer_advance(&k, 1400, 100) && !k.telegraph.pressed);
    assert(keyer_advance(&k, 1500, 100) && k.symbol == '.');
    assert(keyer_advance(&k, 1600, 100));
    assert(keyer_advance(&k, 1700, 100) && k.symbol == '-');
    keyer_cancel(&k);
    assert(keyer_advance(&k, 2500, 100) && !k.telegraph.pressed);
    free(k.telegraph.morse.history);
}

int main(void)
{
    test_four_speeds();
    test_switch_and_overflow();
    test_both_held_and_no_catchup();
    puts("PASS: dual keyer four speeds, FIFO, gaps, word boundary, safe mode switch, overflow, held arbitration, no catchup");
}
