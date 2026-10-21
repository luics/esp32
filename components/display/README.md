# Display component

乐鑫托管驱动 `espressif/esp_lcd_ili9341` + ESP-IDF `esp_lcd`，驱动 SPI 接口的
240×320 ILI9341 TFT-LCD。该型号为彩色 LCD，组件名使用 `display`。

任意颜色按钮按下后，屏幕显示对应红/绿/蓝背景，并居中显示“红色/绿色/蓝色”。
文字为 32×32 字模，两字含 8 像素间距；绿色背景用黑字，红、蓝背景用白字。
画面保持到下次事件，上电为黑屏。

```c
#include "display.h"

display_handle_t screen = NULL;
const display_config_t config = {
    .sclk_gpio = CONFIG_DISPLAY_SCLK_GPIO, .mosi_gpio = CONFIG_DISPLAY_MOSI_GPIO,
    .cs_gpio = CONFIG_DISPLAY_CS_GPIO, .dc_gpio = CONFIG_DISPLAY_DC_GPIO,
    .reset_gpio = CONFIG_DISPLAY_RST_GPIO,
};
ESP_ERROR_CHECK(display_init(&config, &screen));
ESP_ERROR_CHECK(display_show_color(screen, DISPLAY_COLOR_RED));
```

调用方声明 `PRIV_REQUIRES display`。初始化在启动阶段调用一次，实例随应用持续运行；
当前不提供运行时销毁接口。组件独占 SPI2，像素时钟 40 MHz，颜色格式为 RGB565。
面板初始化使用 BGR 子像素顺序及横向扫描翻转，适配连接器朝下的 240×320 竖屏；
绘图缓冲仍为标准 RGB565，像素按高字节优先发送。
`display_show_color()` 从任务或按钮回调调用，立即提交颜色；不适用于 ISR。
后台任务独占屏幕，只保留最新颜色，不积压旧画面；每传完 16 行检查新事件并切换刷新。
一帧的像素数据约需 31 ms 传输，实际可见延迟还包含消抖、调度及渲染时间。
DMA 传输完成后才重用行缓冲，图像渐进刷新，不保证整屏同时切换。
显示刷新不阻塞闪灯或蜂鸣器任务，驱动错误会记录日志。

| 屏幕引脚 | 默认 GPIO |
| --- | --- |
| SCK | 6 |
| MOSI | 7 |
| CS | 18 |
| D/C | 19 |
| RST | 20 |

默认引脚在组件 Kconfig 中配置，应用检查与已有按钮、LED、蜂鸣器引脚的冲突。
Wokwi 连接 VCC→3V3、GND→GND；其模型不暴露 RST 和背光引脚，复位接线仅用于实板。
MISO 不使用。实板须按所用模块的供电和背光要求接线，默认配置适配 3.3 V 逻辑。

中文点阵来自开源 Noto Sans CJK SC Regular，仅嵌入“红绿蓝色”四字，许可证见
[fonts/OFL.txt](fonts/OFL.txt)。生成方式：

```sh
curl -fL https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf -o /tmp/display-font.otf
uv run --with pillow python tools/generate_display_font.py /tmp/display-font.otf
```

生成脚本同时输出 `/tmp/esp32-display-preview.png` 供检查排版。
构建和仿真见 [项目说明](../../AGENTS.md)。
