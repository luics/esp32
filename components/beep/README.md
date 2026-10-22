# beep

无源低电流压电蜂鸣器：正极 GPIO3、负极 GND。
`CONFIG_BEEP_GPIO` 默认 3，`CONFIG_BEEP_FREQUENCY_HZ` 默认 2000 Hz。
独占 LEDC 低速 timer0/channel0，以 10 位分辨率和 50% 占空比输出。

`beep_init(gpio, frequency_hz)` 只在启动时调用一次，上电静音。
`beep_set_active(bool)` 从应用发送任务串行控制开关，立即返回，不包含延时。
应用统一控制 WS2812 与蜂鸣器：按下发声，松开停声，持续时间跟随实际按住时间，不补播。
旧 `beep_trigger`、运行时音量接口与固定时长配置已移除。
