# display

ILI9341 240×320 竖屏，固定白底黑字，SPI2 独占。
默认 SCK/MOSI/CS/DC/RST = GPIO6/7/18/19/20，配置见组件 Kconfig。
Wokwi LCD 不提供 RST 接口，实板连接 GPIO20；背光按模块要求接线。

`display_init(config, &handle)` 初始化面板及后台刷新任务。
`display_show_frame(handle, &frame)` 复制显示快照到单槽队列，立即返回。
16 行 DMA 缓冲分条刷新，传输完成后再复用；新画面可以取消旧刷新，但不影响应用历史。

`display_frame_t` 包含当前档位、字符锁定单位、输入中的点划及候选字符、历史页码和 10 行历史。
上部显示当前输入，底部 y=169 起为固定历史区域；每行最多 37 个 ASCII 字符。
字体由 `tools/generate_display_font.py` 生成：字母数字采用手工等宽点阵，其余符号来自 Noto Sans CJK SC，许可见 `fonts/NOTICE.md`。

历史归档、字符/词间隔、分组和每 4 秒翻页由 `main/morse.c` 与应用主任务负责。
