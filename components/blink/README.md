# Blink component

使用 `espressif/led_strip` 的 RMT 后端驱动单颗 WS2812，适配
微雪 ESP32-C6-Zero（GPIO8）。引脚由调用方传入，颜色顺序由组件 Kconfig 选择。

```c
#include "blink.h"

blink_handle_t led = NULL;
ESP_ERROR_CHECK(blink_create(CONFIG_BLINK_RGB_GPIO, &led));
ESP_ERROR_CHECK(blink_once_color(led, BLINK_COLOR_RED));
ESP_ERROR_CHECK(blink_delete(led));
```

调用方声明 `PRIV_REQUIRES blink`。

- `blink_create()` 初始化驱动并灭灯；失败时输出句柄为 NULL。
- `blink_once_color()` 支持 `BLINK_COLOR_RED/GREEN/BLUE`，对应通道亮度 64/255，
  亮 500 ms、灭 500 ms，再返回；无效颜色返回 `ESP_ERR_INVALID_ARG`。
- `blink_once()` 保留蓝光 RGB=(0, 0, 64) 的兼容接口。
  延时让出 CPU；只能在任务中调用，同一句柄需串行使用。
- `blink_delete()` 释放驱动；调用前结束所有使用并确保灯已熄灭。
- `blink_run(int gpio_number)` 保留持续闪烁方式，正常运行不返回。

接口返回 `esp_err_t`；非法参数返回 `ESP_ERR_INVALID_ARG`，其他失败返回驱动错误码。
应用启动后保持 LED 关闭，由按钮颜色队列通知主任务调用 `blink_once_color()`。
组件 Kconfig 的 `CONFIG_BLINK_LED_ORDER_GRB` 默认关闭，实板采用 RGB；
Wokwi 独立构建启用 GRB，以正确显示红、绿通道。
依赖版本见 `idf_component.yml`，硬件与仿真差异见 [项目说明](../../AGENTS.md)。
