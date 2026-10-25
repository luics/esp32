# PIR 运动传感器

`pir_sensor_create(CONFIG_PIR_SENSOR_GPIO, &handle)` 将 GPIO 配置为带下拉的数字输入。
主任务每轮调用 `pir_sensor_sample(handle, now_ms, &sample)`；组件仅在高低电平变化时写日志。
`sample.motion` 表示当前高电平，`sample.active_ms` 是持续高电平时间，低电平时归零。

实板将模块 VCC 接 5V、GND 接公共地、OUT 接 GPIO15。接线前按模块丝印确认三个引脚的位置，且 OUT 高电平不得超过 ESP32-C6 的 3.3V 输入范围。
HC-SR501 一类 PIR 只输出数字高低电平，持续时间是应用使用的活动程度代理，并非人体移动幅度测量值。
Wokwi 使用 `wokwi-pir-motion-sensor`，OUT 接 GPIO15，在仿真中点击传感器的“Simulate Motion”。
