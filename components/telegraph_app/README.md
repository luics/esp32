# telegraph_app

电报机应用协调组件，对外仅提供 [telegraph_app_run()](include/telegraph_app.h)。
`main/app_main.c` 在启动任务中调用一次；该函数初始化外设后持续处理事件，不返回。
上电默认 D 双键模式；蓝键通过 `esp_restart()` 重新经过同一启动入口，也恢复为 D。黄色模式键仍可切换到 S。
点击黄色模式键（实板 GPIO14 / Wokwi GPIO2）时以 INFO 输出目标单键/双键模式；发送中切换时注明当前信号结束后生效。
每次启动随机生成三个字符的标签（A–Z、0–9），通过显示快照在底栏页码右侧显示为 `ABC`；运行期间保持不变，复位后重新生成，不保证唯一。生成后以 WARNING 级别输出一次 `label=ABC` 日志。

职责包括引脚冲突检查、外设初始化、按键边沿队列、声光同步、历史翻页和显示快照。
应用任务优先级为 3，高于显示任务的 2；按钮回调只投递事件或复位通知，声光由应用任务串行控制。
GPIO8 板载灯和 GPIO5 外接 WS2812（GRB 顺序）均用于点红/划绿。模式键每次按下时，GPIO5 优先闪黄光 200 ms，随后恢复当前发送颜色或灭灯，采用非阻塞计时。
PIR 组件每轮采样 GPIO15，电平变化和活动档位输出串口日志。GPIO5 空闲时按连续高电平时长分四档闪烁：0–1 秒白光（周期 500 ms）、1–2 秒绿光（400 ms）、2–3 秒黄光（300 ms）、3 秒起红光（150 ms）；各档亮灭各占半个周期，低电平立即结束并归零。优先级为模式黄光、点划颜色、PIR 闪光，GPIO8 只显示点划。PIR 为数字信号，这些档位只代表持续触发时间。
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

启用 Home Assistant 后，通过单槽队列向 [telegraph_ha](../telegraph_ha/README.md) 提交 PIR、实际模式、字符单位及最新确认字符；远程 S/D 请求由输入任务复用 keyer 切换。网络处理在后台，不阻塞按键。
