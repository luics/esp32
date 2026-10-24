# interval_control

GPIO4 / ADC1 CH4 电位器控制摩斯码时间单位，替代原 volume_control；两端接 3V3/GND，滑臂接 GPIO4。

`interval_control_start(gpio, sample_interval_ms)` 初始化 ADC 并启动采样任务；
`interval_control_get_unit_ms()` 原子读取当前 200/150/100/60 ms 档位，未启动时返回 `INTERVAL_CONTROL_DEFAULT_UNIT_MS`（100 ms）。
开关：`CONFIG_INTERVAL_CONTROL_ENABLED`。实板 defaults 关闭，应用不初始化 ADC、不创建采样任务，固定使用 100 ms，GPIO4 不参与占用检查。
接好电位器后在 menuconfig 启用；GPIO 和采样周期选项随后可见。Wokwi defaults 默认启用，保留旋钮变速。
启用时配置：`CONFIG_INTERVAL_CONTROL_ADC_GPIO=4`、`CONFIG_INTERVAL_CONTROL_SAMPLE_INTERVAL_MS=50`。

12 位 ADC 初始值按四个等宽区间映射：0～1023→200、1024～2047→150、2048～3071→100、3072～4095→60 ms。
运行中阈值两侧各带 80 计数迟滞，避免临界抖动。启用后上电采用实际位置；Wokwi value=640，初始位置在第三档中部，默认 100 ms。
只发布档位，不直接控制声音；应用在新字符开始时锁定单位，保证当前字符时序一致。

启用时，启动就绪、首次采样和档位变化输出 INFO；档位稳定时不逐次打印，ADC 读取失败输出 DEBUG。
未接电位器时应关闭采样开关，避免悬空输入导致档位变化；关闭时应用仅输出一次固定 100 ms 的 INFO 日志。
