# 摩斯码电报机

微雪 ESP32-C6-Zero，ESP-IDF v6.1，EIM 环境 `stable-mcp`，8 MB Flash。

## 操作

- 红色按钮 GPIO10：每次按下发送一个点 `.`，红光及蜂鸣器持续 1 单位。
- 绿色按钮 GPIO9/BOOT：每次按下发送一个划 `-`，绿光及蜂鸣器持续 3 单位。
- 蓝色按钮已移除；长按只触发一次。复位时松开 BOOT。
- GPIO4 电位器为 **Interval control**：从低到高四档 200（默认）、150、100、60 ms。
  档位切换带迟滞；当前字符的单位保持不变，新档位从下一个字符开始使用。
  实板上电使用旋钮实际位置，默认 200 ms 对应旋钮最低端。
- 快速点击按 FIFO 排队，最多等待 32 个点划；相邻信号至少静默 1 单位。
  满队列后拒绝新点击并打印告警，避免静默覆盖旧输入。
- 静默从声光实际结束开始计算：3 单位确认字符，7 单位确认单词，间隔不叠加。
  若只需同一字符的下一个点划，应在前一个信号结束后 3 单位内点击。

## 屏幕

ILI9341 240×320 始终白底黑字，上方显示档位、当前字符使用的单位、输入中的点划及候选解码。
候选字符可能随新点划变化，例如 `.` 为 E，追加 `-` 后为 A；只有静默满 3 单位才提交历史。
支持 A–Z、0–9 及常用标点，未知编码显示 `?`，超过 8 个点划显示 `INVALID`。

底部固定为历史区域，不包含输入中的字符。每组先显示摩斯码，再以 `>` 显示解码文本。
静默满 7 单位后按词合并；尚未确认单词边界的历史按字符分组。普通词尽量不跨页，超长词换行续页。
新增历史先显示最新页，随后每 4 秒循环翻页，覆盖本次开机全部记录。
历史动态保存在 RAM，断电清空；每个字符确认时同时输出串口日志。RAM 耗尽时报告错误并停止，
不静默丢弃旧历史。开发板上的常亮红色电源灯与 WS2812 信号灯不同。

## 接线

| 功能 | 端口 |
|---|---|
| 点按钮（红） | GPIO10 ↔ GND |
| 划按钮（绿 / BOOT） | GPIO9 ↔ GND |
| WS2812 DIN | GPIO8，实板 RGB 顺序 |
| 无源低电流压电蜂鸣器 | 正极 GPIO3，负极 GND，2 kHz |
| 间隔电位器 | 两端 3V3 / GND，滑臂 GPIO4 / ADC1 CH4 |
| LCD SCK / MOSI | GPIO6 / GPIO7 |
| LCD CS / DC / RST | GPIO18 / GPIO19 / GPIO20 |

SPI2 由显示组件独占；LEDC 低速 timer0/channel0 由蜂鸣器独占。
Wokwi 使用 DevKitC-1 和独立 NeoPixel 替代板载 LED，颜色顺序为 GRB；LCD 仿真不接 RST。
保留 UART 与 USB Serial/JTAG 日志。

## 构建与验证

修改 Kconfig/defaults 后先取得重新配置授权，再执行：

```sh
eim run "idf.py reconfigure build" stable-mcp
eim run "bash tools/build_wokwi.sh reconfigure build" stable-mcp
```

实板配置与产物在根目录 `sdkconfig`、`build/`；仿真配置与产物在 `build-wokwi/`，互不覆盖。
VS Code 运行 **Wokwi: Start Simulator**。红/绿按钮支持 R/G 快捷键；旋钮默认最低档 200 ms。

主机逻辑测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -I main main/morse.c tests/test_morse.c -o /tmp/esp32-test-morse
/tmp/esp32-test-morse
```

仿真检查：确认白底黑字、上电灭灯静音、四档速度；R/G 分别产生 1/3 单位红/绿光及同步声音；
长按不重复、快速点击不截断，输入 `.... . .-.. .-.. --- / .-- --- .-. .-.. -..` 后得到 HELLO WORLD。
检查 3/7 单位自动提交、当前输入不进入历史、词分组以及超过一页后的循环翻页。
实板烧录需另行明确授权端口和设备。

组件接口见各组件 README；代理开发规范见 [AGENTS.md](AGENTS.md)。
