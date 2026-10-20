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
