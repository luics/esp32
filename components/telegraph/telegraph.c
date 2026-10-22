#include "telegraph.h"

bool telegraph_advance(telegraph_t *telegraph, uint64_t now_ms)
{
    /* 按住期间暂停字符/词提交；松开后才从停声时刻计算静默。 */
    if (telegraph->pressed) return true;
    return morse_advance(&telegraph->morse, now_ms);
}

bool telegraph_press(telegraph_t *telegraph, uint64_t now_ms, uint32_t selected_unit_ms)
{
    if (telegraph->pressed) return true;
    /* 按实际按下时刻结算旧字符，换档只作用于下一个新字符。 */
    if (!telegraph_advance(telegraph, now_ms)) return false;
    telegraph->pressed = true;
    telegraph->pressed_ms = now_ms;
    telegraph->pressed_unit_ms = telegraph->morse.current[0] ? telegraph->morse.unit_ms : selected_unit_ms;
    return true;
}

char telegraph_release(telegraph_t *telegraph, uint64_t now_ms)
{
    if (!telegraph->pressed) return 0;
    telegraph->pressed = false;
    if (now_ms < telegraph->pressed_ms) return 0;
    const uint64_t duration = now_ms - telegraph->pressed_ms;
    const char symbol = duration >= 3ULL * telegraph->pressed_unit_ms ? '-' : '.';
    /* 声光已由应用关闭，只记录实际按住得到的点划，不启动任何补播。 */
    morse_begin(&telegraph->morse, symbol, telegraph->pressed_unit_ms);
    morse_end(&telegraph->morse, now_ms);
    return symbol;
}
