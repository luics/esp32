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
主应用在回调中发送任务通知，在主任务中逐次调用 `blink_once()`，避免阻塞按钮检测。

组件 Kconfig 的 `CONFIG_BUTTON_INPUT_GPIO` 默认 9，运行时引脚由调用方传入。
C6-Zero 的 BOOT 按钮按下将 GPIO9 接地；复位时应松开，否则可能进入下载模式。
外接按钮同样连接 GPIO 与 GND，使用前确认引脚支持上拉且未被其他外设占用。
Wokwi 绿色按钮连接 GPIO9，可点击或在仿真图获得焦点后按 B；默认保留模拟机械抖动。

驱动文档：[espressif/button](https://components.espressif.com/components/espressif/button/versions/4.2.2/readme)。
构建与仿真见 [项目说明](../../AGENTS.md)。
