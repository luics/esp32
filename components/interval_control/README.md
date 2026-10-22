# interval_control

GPIO4 / ADC1 CH4 电位器控制摩斯码时间单位，替代原 volume_control；两端接 3V3/GND，滑臂接 GPIO4。

`interval_control_start(gpio, sample_interval_ms)` 初始化 ADC 并启动采样任务；
`interval_control_get_unit_ms()` 原子读取当前 200/150/100/60 ms 档位，未启动时返回 200。
配置：`CONFIG_INTERVAL_CONTROL_ADC_GPIO=4`、`CONFIG_INTERVAL_CONTROL_SAMPLE_INTERVAL_MS=50`。

12 位 ADC 初始值按四个等宽区间映射：0～1023→200、1024～2047→150、2048～3071→100、3072～4095→60 ms。
运行中阈值两侧各带 80 计数迟滞，避免临界抖动。上电采用实际位置；Wokwi value=0，默认 200 ms。
只发布档位，不直接控制声音；应用在新字符开始时锁定单位，保证当前字符时序一致。
