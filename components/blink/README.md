# Blink component

使用 `espressif/led_strip` 的 RMT 后端驱动 WS2812，每个句柄独占一颗灯与 RMT 通道。
当前工程同时使用 GPIO8 板载点划灯与 GPIO5 外接模式提示灯。

```c
#include "blink.h"

blink_handle_t led = NULL;
ESP_ERROR_CHECK(blink_create(CONFIG_BLINK_RGB_GPIO, &led));
ESP_ERROR_CHECK(blink_once_color(led, BLINK_COLOR_RED));
ESP_ERROR_CHECK(blink_delete(led));
```

调用方声明 `PRIV_REQUIRES blink`。

- `blink_create()` 按组件 Kconfig 选择板载灯颜色顺序，初始化后灭灯；失败时输出句柄为 NULL。
- `blink_create_with_order()` 允许为每颗灯指定 `BLINK_ORDER_RGB/GRB`，外接标准 WS2812 使用 GRB。
- `blink_set_color()` 立即显示红、绿、蓝或黄光，`blink_off()` 立即灭灯；不包含亮灭延时，
  数据发送同步完成，同一句柄须串行使用。
- `blink_once_color()` 支持 `BLINK_COLOR_RED/GREEN/BLUE/YELLOW`，对应通道亮度 64/255，
  亮 200 ms、灭 200 ms，再返回；无效颜色返回 `ESP_ERR_INVALID_ARG`。
- `blink_once()` 保留蓝光 RGB=(0, 0, 64) 的兼容接口。
  延时让出 CPU；只能在任务中调用，同一句柄需串行使用。
- `blink_delete()` 释放驱动；调用前结束所有使用并确保灯已熄灭。
- `blink_run(int gpio_number)` 保留持续闪烁方式，正常运行不返回。

接口返回 `esp_err_t`；非法参数返回 `ESP_ERR_INVALID_ARG`，其他失败返回驱动错误码。
应用启动时两颗灯均关闭。GPIO8 跟随点划发送显示红/绿光；GPIO5 在模式按钮每次消抖按下时
亮黄光 200 ms，主循环非阻塞计时后关闭，连续点击重新计时，不中断点划声光。
组件 Kconfig 的 `CONFIG_BLINK_LED_ORDER_GRB` 默认关闭，实板采用 RGB；
Wokwi 独立构建启用 GRB，以正确显示红、绿通道。
依赖版本见 `idf_component.yml`，硬件与仿真差异见 [项目说明](../../AGENTS.md)。
