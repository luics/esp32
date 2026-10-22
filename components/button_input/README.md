# button_input

封装 espressif/button 的 GPIO 按钮，内部上拉，低电平有效，驱动消抖。
`button_input_create(gpio, callback, context, &handle)` 初始化并注册 `BUTTON_PRESS_DOWN` 回调。
每次按下回调一次，长按不重复；回调在任务上下文执行，应快速返回。

红色点按钮 `CONFIG_BUTTON_INPUT_RED_GPIO=10`，绿色划按钮 `CONFIG_BUTTON_INPUT_GPIO=9`。
按钮另一端接 GND。蓝色按钮及其 GPIO 配置已移除。
GPIO9/BOOT 为启动配置引脚，复位时保持松开。
应用回调只投递点划事件；定长声光与历史处理在应用发送任务执行。
