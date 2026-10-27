#include "ha_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ha_rx_t rx = {0};
    const char *command = "luics/test/led/set";
    const size_t n = strlen(command);
    /* 长度界定的输入可以没有结束符；ON/OFF 是幂等设值操作。 */
    const char dual[] = {'O', 'N', 'X'};
    assert(ha_receive(&rx, command, command, n, dual, 2, 0, 2, false) == HA_ON);
    assert(ha_receive(&rx, command, command, n, "OFF", 3, 0, 3, false) == HA_OFF);
    assert(ha_receive(&rx, command, command, n, "ON", 2, 0, 2, true) == HA_NONE);
    assert(ha_receive(&rx, command, command, n - 1, "D", 1, 0, 1, false) == HA_NONE);
    assert(ha_receive(&rx, command, command, n, "D\0", 2, 0, 2, false) == HA_NONE);
    assert(ha_receive(&rx, command, command, n, "online!", 7, 0, 7, false) == HA_NONE);
    for (size_t split = 1; split < 6; ++split) {
        assert(ha_receive(&rx, command, "homeassistant/status", 20, "online", split, 0, 6, true) == HA_NONE);
        assert(ha_receive(&rx, command, NULL, 0, &"online"[split], 6 - split, split, 6, false) == HA_REDISCOVER);
    }
    assert(ha_receive(&rx, command, "homeassistant/status", 20, "on", 2, 0, 6, false) == HA_NONE);
    assert(ha_receive(&rx, command, NULL, 0, "ine", 3, 3, 6, false) == HA_NONE);
    assert(ha_receive(&rx, command, NULL, 0, "line", 4, 2, 6, false) == HA_NONE);
    assert(ha_receive(&rx, command, "homeassistant/status", 20, "offline", 7, 0, 7, false) == HA_NONE);
    assert(ha_receive(&rx, command, NULL, 0, "D", 1, (size_t)-1, 1, false) == HA_NONE);
    puts("PASS: bounded MQTT fragments, exact topics/payloads, retained command rejection and HA birth recovery");
}
