# Beep volume control component

使用 ESP32-C6 ADC1 读取单联 0～10 kΩ 电位器，并把滑臂电压换算为蜂鸣器
0～100% 音量。后台任务默认每 50 ms 采样一次；变化达到 2 个百分点才更新，
端点立即更新。采样和音量变化不会阻塞按钮回调，鸣叫进行中调节旋钮也会立即生效。

```c
#include "volume_control.h"

ESP_ERROR_CHECK(volume_control_start(CONFIG_VOLUME_CONTROL_ADC_GPIO,
                                     CONFIG_VOLUME_CONTROL_SAMPLE_INTERVAL_MS));
```

应用先调用 `beep_init()`，再启动此组件。GPIO 和采样周期在本组件 Kconfig 中配置；
微雪 ESP32-C6-Zero 默认使用 GPIO4（ADC1 通道4）。`volume_control_start()` 只调用一次，
成功后采样任务随应用运行。组件通过 `beep_set_volume()` 更新输出；0% 为静音。

接线：电位器两端接 3V3 和 GND，滑臂 SIG 接 GPIO4。Wokwi 使用
`wokwi-potentiometer`，其 `value` 属性范围为 0～1023；仿真图默认设在中间位置。
实板请使用不超过 3.3 V 的电位器输出。蜂鸣器本身仍连接 GPIO3 与 GND。

构建与仿真步骤见 [项目说明](../../AGENTS.md)。
