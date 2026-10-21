# Blink component

本地普通 GPIO LED 闪灯组件，使用 ESP-IDF 内置 GPIO 驱动，无外部依赖。

## 接口

```c
#include "blink.h"

ESP_ERROR_CHECK(blink_run(CONFIG_BLINK_GPIO));
```

调用方在组件的 `CMakeLists.txt` 中声明 `PRIV_REQUIRES blink`。

`blink_run(int gpio_number)` 配置指定引脚为输出，以高电平点亮普通 LED，
每次亮、灭各保持 500 ms。该函数在当前任务中持续运行，不创建后台任务，
正常情况下不会返回；延时会让出 CPU。无效的输出引脚返回
`ESP_ERR_INVALID_ARG`，GPIO 驱动错误原样返回，由调用方处理。

组件不读取项目配置。当前应用通过 `main/Kconfig.projbuild` 中的
`CONFIG_BLINK_GPIO` 选择引脚并在 `main/app_main.c` 中传入。
实际接线为 GPIO → 330Ω 电阻 → LED 阳极，LED 阴极 → GND。
不适用于可寻址 RGB LED；接线前需确认目标板的引脚用途。

## 当前开发板

项目默认使用 ESP32-C5-DevKitC-1，ESP-IDF 目标为 `esp32c5`，Flash 配置为
8 MB，未启用 PSRAM。外接红色 LED 接线为 GPIO6 → 330Ω 电阻 → LED 阳极，
LED 阴极 → GND。亮、灭各保持 500 ms。

GPIO2 是 C5 的启动绑带引脚，不沿用旧 C6 示例的接线。板载 GPIO27 是
可寻址 RGB LED，本组件不控制它。硬件接线参考
[官方开发板指南](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp32-c5-devkitc-1/user_guide.html)。

经确认允许替换构建配置后，在项目根目录执行：

```sh
eim run "idf.py set-target esp32c5"
eim run "idf.py build"
```

Wokwi 使用 `board-esp32-c5-devkitc-1`，连接端点为 `esp:6` 和 `esp:GND.1`。
更改板型后关闭旧模拟器，再执行 `Wokwi: Start Simulator`。Wokwi 的 C5
支持目前标记为 [alpha](https://docs.wokwi.com/guides/esp32)，不能替代实板验证。
