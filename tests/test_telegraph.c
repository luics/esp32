#include "telegraph.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_release_commits_without_playback(void)
{
    telegraph_t t = {0};
    assert(telegraph_press(&t, 10, 100));
    assert(!t.morse.current[0]);
    assert(telegraph_release(&t, 40) == '.');
    /* 松开立即记录点，静默从本次松开算起，不等待定长补播。 */
    assert(!strcmp(t.morse.current, ".") && t.morse.ended_ms == 40);
    assert(telegraph_advance(&t, 339));
    assert(!t.morse.count);
    assert(telegraph_advance(&t, 340));
    assert(t.morse.count == 1 && t.morse.history[0].decoded == 'E');
    free(t.morse.history);
}

static void test_hold_thresholds(void)
{
    const uint32_t units[] = {200, 150, 100, 60};
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); ++i) {
        const uint32_t unit = units[i];
        const uint64_t durations[] = {0, unit - 1, unit, 2 * unit, 3 * unit - 1, 3 * unit, 20 * unit};
        for (size_t j = 0; j < sizeof(durations) / sizeof(durations[0]); ++j) {
            telegraph_t t = {0};
            assert(telegraph_press(&t, 100, unit));
            assert(telegraph_press(&t, 100, 60)); /* 重复按下不重置时刻或单位。 */
            assert(telegraph_advance(&t, 100 + durations[j]));
            assert(!t.morse.count && !t.morse.current[0]);
            const char expected = durations[j] >= 3 * unit ? '-' : '.';
            const uint64_t end = 100 + durations[j];
            assert((int)telegraph_release(&t, end) == expected);
            assert(telegraph_release(&t, end + 1) == 0);
            assert(t.morse.unit_ms == unit && t.morse.ended_ms == end);
            assert(telegraph_advance(&t, end + 3 * unit - 1));
            assert(!t.morse.count);
            assert(telegraph_advance(&t, end + 3 * unit));
            assert(t.morse.count == 1 && t.morse.history[0].decoded == (expected == '-' ? 'T' : 'E'));
            free(t.morse.history);
        }
    }
}

static void test_hold_preserves_character_and_speed(void)
{
    telegraph_t t = {0};
    assert(telegraph_press(&t, 0, 100));
    assert(telegraph_release(&t, 50) == '.');
    /* 静默未满三单位时开始长按，超过字符/词阈值也不提前提交。 */
    assert(telegraph_press(&t, 150, 60));
    assert(t.pressed_unit_ms == 100);
    assert(telegraph_advance(&t, 1000));
    assert(!t.morse.count && !strcmp(t.morse.current, "."));
    assert(telegraph_release(&t, 1000) == '-');
    assert(!strcmp(t.morse.current, ".-") && t.morse.unit_ms == 100);
    assert(telegraph_advance(&t, 1300));
    assert(t.morse.count == 1 && t.morse.history[0].decoded == 'A');
    assert(telegraph_press(&t, 1700, 60));
    assert(t.pressed_unit_ms == 60 && t.morse.history[0].word_end);
    assert(telegraph_release(&t, 1880) == '-');
    assert(t.morse.unit_ms == 60);
    free(t.morse.history);
}

static void test_rapid_edges_and_actual_stop(void)
{
    telegraph_t t = {0};
    for (size_t i = 0; i < 100; ++i) {
        const uint64_t down = i * 60;
        assert(telegraph_press(&t, down, 100));
        assert(telegraph_release(&t, down + 20) == '.');
        assert(t.morse.ended_ms == down + 20);
        assert(telegraph_release(&t, down + 21) == 0);
    }
    assert(!t.morse.count && t.morse.invalid); /* 无定长播放/最小静默等待，边沿均即时处理。 */
    assert(telegraph_advance(&t, t.morse.ended_ms + 300));
    assert(t.morse.count == 1 && !strcmp(t.morse.history[0].code, "INVALID"));
    free(t.morse.history);

    memset(&t, 0, sizeof(t));
    assert(telegraph_press(&t, 0, 100));
    assert(telegraph_release(&t, 299) == '.'); /* 松开事件时间决定阈值，不能用后续关声耗时。 */
    morse_end(&t.morse, 305); /* 应用在实际关声后更新静默起点。 */
    assert(telegraph_advance(&t, 604) && !t.morse.count);
    assert(telegraph_advance(&t, 605) && t.morse.count == 1);
    free(t.morse.history);
}

int main(void)
{
    test_release_commits_without_playback();
    test_hold_thresholds();
    test_hold_preserves_character_and_speed();
    test_rapid_edges_and_actual_stop();
    puts("PASS: release commits without replay, four-speed thresholds, hold timeout pause, speed lock, rapid edges and actual stop timing");
}
