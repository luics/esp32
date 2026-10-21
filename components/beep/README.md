# Beep component

使用 ESP-IDF LEDC 驱动外接无源压电蜂鸣器。默认 GPIO3、2 kHz、100 ms，
任意颜色按钮按下时发出一声提示音，上电保持静音，长按不重复。

```c
#include "beep.h"

ESP_ERROR_CHECK(beep_init(CONFIG_BEEP_GPIO, CONFIG_BEEP_FREQUENCY_HZ,
                          CONFIG_BEEP_DURATION_MS));
ESP_ERROR_CHECK(beep_set_volume(50));
ESP_ERROR_CHECK(beep_trigger());
```

调用方声明 `PRIV_REQUIRES beep`。参数由调用方传入，对应默认值存放在组件 Kconfig。
`beep_init()` 仅在启动阶段调用一次；成功后专用任务随应用持续运行，无运行时销毁接口。
组件独占 LEDC 低速 timer 0/channel 0，其他组件应使用不同资源。
`beep_trigger()` 可从任务和按钮回调调用，立即返回；不可在 ISR 调用。
蜂鸣器独立于闪灯队列工作，鸣叫期间再次触发会重新计时，不排队补播旧声音。
多个未处理的通知会合并，连续快速按下可能听起来是一段延长的声音。
时长精度为一个 FreeRTOS 系统节拍，运行时驱动错误会输出日志并尝试静音。
`beep_set_volume()` 接收 0~100 的音量百分比，0 为静音；播放期间更新会立即生效。

Wokwi 蜂鸣器引脚 2（正）连接 GPIO3，引脚 1（负）连接 GND。
微雪 C6-Zero 无板载蜂鸣器，实板需外接适配 3.3 V GPIO 的低电流压电蜂鸣器；
需要更大驱动电流的报警器应通过晶体管/驱动模块连接，不能直接由 GPIO 供电。

构建和仿真步骤见 [项目说明](../../AGENTS.md)。
