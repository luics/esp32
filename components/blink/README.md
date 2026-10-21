# Blink component

使用乐鑫托管组件 `espressif/led_strip` 的 RMT 后端，驱动单颗 RGB 顺序的
WS2812。当前应用适配微雪 ESP32-C6-Zero 板载灯（GPIO8）。

## 接口

```c
#include "blink.h"

ESP_ERROR_CHECK(blink_run(CONFIG_BLINK_RGB_GPIO));
```

调用方在组件的 `CMakeLists.txt` 中声明 `PRIV_REQUIRES blink`。

`blink_run(int gpio_number)` 在指定数据引脚上初始化单颗 WS2812，以
RGB=(0, 0, 64) 的蓝光亮 500 ms、灭 500 ms，循环运行。
延时会让出 CPU；函数在当前任务中运行，不创建后台任务，正常情况下不会返回。
无效输出引脚返回 `ESP_ERR_INVALID_ARG`；初始化或发送失败时返回驱动错误码，
成功创建驱动后若出现错误，会尝试释放 RMT 通道和驱动内存。

组件不读取项目配置，引脚由调用方传入。颜色顺序固定为微雪要求的 RGB，
不适用于普通 GPIO LED；其他 WS2812 开发板也需先核对颜色顺序。
依赖及 ESP-IDF 版本约束见 `idf_component.yml`。

Wokwi 替代板型、构建方式和实板差异见项目根目录 [README](../../README.md)。
