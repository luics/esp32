# display

ILI9341 240×320 竖屏，固定白底黑字，SPI2 独占。
默认 SCK/MOSI/CS/DC/RST = GPIO6/7/18/19/20，配置见组件 Kconfig。
真实硬件为 1.5 英寸 SSD1327，128×128、16 级灰度、SPI/I2C；当前实板驱动尚未迁移到 SSD1327。
Wokwi 构建通过 `CONFIG_DISPLAY_WOKWI_SH1107` 选择 SH1107 128×128 黑白 OLED 近似，不能验证 SSD1327 协议或灰度。
仿真仅 I2C：SCL/SDA = GPIO6/7，地址 0x3C、400 kHz，CS/DC/RST 不使用。
Wokwi 元件见 [官方说明](https://docs.wokwi.com/parts/board-grove-oled-sh1107)，SH1107 驱动使用乐鑫托管组件。

`display_init(config, &handle)` 初始化面板及后台刷新任务。
`display_show_frame(handle, &frame)` 复制显示快照到单槽队列，立即返回。
ILI9341 用 16 行 DMA 缓冲；SH1107 共用文字渲染，转为 2048 字节的纵向页缓冲，通过同步 I2C 刷新。
新画面可以取消旧刷新，但不影响应用历史；SH1107 转换补偿 Wokwi 模型的 96 列可见原点。

`display_frame_t` 包含当前档位、字符锁定单位、新词指示 `word_ready`、模式 `dual_mode`、输入中的点划及候选字符、历史页码和 10 行历史。
上部显示当前输入，底部 y=145 起为固定历史区域；每行最多 37 个 ASCII 字符，历史格式为 `> {字符或单词} {摩斯码}`，超宽自动换行。
不显示 SET 行；状态行固定在屏幕底部，左侧页码 `x/x`，右侧 `S xxxMS` / `D xxxMS`（单键 / 双键模式；ILI9341 y=309，SH1107 y=117）。
SH1107 历史内容从 y=55 开始，每页 6 行、每行 20 字符；`DISPLAY_HISTORY_ROWS/COLS` 提供当前视口尺寸，
应用将它们传给 `morse_history_lines`，在分页时处理换行，保证超宽和超长词可完整分页浏览。
字体由 `tools/generate_display_font.py` 生成：字母数字采用手工等宽点阵，其余符号来自 Noto Sans CJK SC，许可见 `fonts/NOTICE.md`。

历史按最新字符/词组优先展示，词内文字和点划保持输入顺序；新增历史回到第一页首行。
历史归档、字符/词间隔与分组由 `morse`、`telegraph` 和 `telegraph_app` 组件负责。
绿键每次按下切换一页，末页回到首页；不再自动轮播。

右上角直径 5 像素的小黑点表示可以开始新词：上电显示，单键红键按下或双键信号开始时隐藏，信号结束后静默满 7 个锁定单位再显示并保持。
状态由应用复用解码器的词间隔判定，通过 `word_ready` 传入；长按期间保持隐藏，后续有效输入重新开始等待，换档与绿键翻页不重置已有静默。
显隐变化先更新指示区域，再刷新正文；ILI9341 更新 7×7 区域，SH1107 更新覆盖指示点的前两个 8 像素页并保留同页文字。空闲时不再周期刷新或闪烁。
