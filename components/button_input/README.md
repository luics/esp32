# Button input component

封装乐鑫托管驱动 `espressif/button`，提供低电平按下、内部上拉的软件消抖按钮。
每次按下回调一次，长按不重复，连续点击分别通知。句柄随应用运行持续保留。

```c
#include "button_input.h"

button_input_handle_t button = NULL;
ESP_ERROR_CHECK(button_input_create(CONFIG_BUTTON_INPUT_GPIO, on_press, context, &button));
```

调用方声明 `PRIV_REQUIRES button_input`。`on_press(void *context)` 在驱动任务中执行，
必须快速返回，不能调用阻塞操作；回调及 context 在按钮使用期间保持有效。
初始化失败返回 `esp_err_t`，输出句柄为 NULL。本组件不创建闪光任务，也不直接依赖 blink。
主应用为三个按钮分别创建实例，在回调中覆盖单槽队列里的旧颜色，主任务立即调用
`blink_set_color()` 切色并重新计时 200 ms；期间再次按下会立即更新颜色和计时，
到期无新事件才灭灯，不积压或补播旧颜色。同色再次按下会延长亮灯时间。
同一回调还调用 `beep_trigger()`，通过独立后台任务发出短鸣，不等待闪灯结束。
声音配置及接线见 [beep 说明](../beep/README.md)。

组件 Kconfig 的引脚默认值如下，运行时由调用方传入：

| 按钮 | 配置 | GPIO | Wokwi 快捷键 |
| --- | --- | --- | --- |
| 红 | `CONFIG_BUTTON_INPUT_RED_GPIO` | 10 | R |
| 绿 | `CONFIG_BUTTON_INPUT_GPIO` | 9 | G |
| 蓝 | `CONFIG_BUTTON_INPUT_BLUE_GPIO` | 11 | B |


C6-Zero 的 BOOT 按钮按下将 GPIO9 接地；复位时应松开，否则可能进入下载模式。
外接按钮同样连接 GPIO 与 GND，使用前确认引脚支持上拉且未被其他外设占用。
实板需外接红、蓝按钮，分别连接 GPIO10/GPIO11 与 GND；绿色可使用 BOOT。
Wokwi 三个按钮默认保留模拟机械抖动，快捷键需仿真图获得焦点。

驱动文档：[espressif/button](https://components.espressif.com/components/espressif/button/versions/4.2.2/readme)。
构建与仿真见 [项目说明](../../AGENTS.md)。
