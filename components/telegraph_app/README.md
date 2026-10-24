# telegraph_app

电报机应用协调组件，对外仅提供 [telegraph_app_run()](include/telegraph_app.h)。
`main/app_main.c` 在启动任务中调用一次；该函数初始化外设后持续处理事件，不返回。
上电默认 D 双键模式；蓝键通过 `esp_restart()` 重新经过同一启动入口，也恢复为 D。灰键仍可切换到 S。
点击灰键（默认 GPIO2）时以 INFO 输出目标单键/双键模式；发送中切换时注明当前信号结束后生效。
每次启动随机生成三个字符的标签（A–Z、0–9），通过显示快照在底栏页码右侧显示为 `ABC`；运行期间保持不变，复位后重新生成，不保证唯一。生成后以 WARNING 级别输出一次 `label=ABC` 日志。

职责包括引脚冲突检查、外设初始化、按键边沿队列、声光同步、历史翻页和显示快照。
应用任务优先级为 3，高于显示任务的 2；按钮回调只投递事件或复位通知，声光由应用任务串行控制。
单键声光跟随按住时间；双键由 `telegraph` 决定标准发送状态，实际关声后校正静默起点。
显示组件只接收快照，解码/历史归属 `morse`，输入模式/发送状态归属 `telegraph`。
历史变更时将快照交给 `telegraph_sync`，按词异步 POST/PATCH，三个字段始终包含 `morse`、`content`、本次开机的 `label`。
联网与错误重试不在输入主循环执行；Wi-Fi 配置、UUID 归属及失败处理见 [telegraph_sync](../telegraph_sync/README.md)。

依赖全部通过 `PRIV_REQUIRES` 声明；公共接口不暴露外设或 FreeRTOS 类型。
GPIO 及显示配置沿用现有 `CONFIG_*`，初始化与运行错误沿用 `ESP_ERROR_CHECK` 处理。
组件随本次开机持续运行，不提供第二套重置或重新初始化流程。

主机验证：在项目根目录运行 `python3 tools/test_host.py`，其中外设替身测试同时编译真实 `app_main` 与本组件。
固件验证：按 [AGENTS.md](../../AGENTS.md) 的实板/Wokwi 两版流程构建；实板烧录仍需用户授权。

间隔电位器由 `CONFIG_INTERVAL_CONTROL_ENABLED` 控制；关闭时不启动 ADC 采样、不将电位器引脚列为占用，使用固定 100 ms。实板默认关闭，Wokwi 默认启用。
