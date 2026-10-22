# telegraph

纯 C 电报输入与发送状态机，公开依赖 `morse`，不依赖 ESP-IDF、GPIO、声光驱动或系统时钟。
公共头文件放在 `include/`，应用通过消抖边沿和毫秒时间推进状态。

- [telegraph.h](include/telegraph.h)：单键按住时长识别，锁定字符单位，按住期间暂停字符/词提交。
- [keyer.h](include/keyer.h)：支持 S 单键模式；D 双键模式提供 64 槽点划 FIFO、标准 1/3 单位发送、
  1 单位静默、长按重复及模式切换。显式点击优先，两键同时长按时交替。

全零初始化为 S；当前电报机应用以 `keyer_t keyer = {.dual_mode = true, .target_dual = true};` 默认启动 D。
初始化后串行调用 `keyer_edge`、`keyer_advance` 和 `keyer_toggle_mode`。
返回 `false` 表示历史内存分配失败；`overflow` 表示缓存溢出，调用方应报告并等待物理松开。
`keyer_cancel` 取消未完成发送及缓存，保留已输入历史；模式切换等待当前信号结束，并取消未发送缓存及跨模式长按。

本组件只决定发送状态；实际声光开关、停声后静默时刻修正和显示交给 `telegraph_app`。
实例结束时释放其拥有的 `keyer.telegraph.morse.history`，持有历史后避免浅拷贝。
主机验证：在项目根目录运行 `python3 tools/test_host.py`。
