# morse

纯 C 摩斯码模型：A–Z、0–9、常用标点解码，3/7 单位字符/词边界，完整历史及最新记录优先分页。
不依赖 ESP-IDF、外设或系统时钟；通过调用方传入的毫秒时间推进状态。

公共接口见 [morse.h](include/morse.h)：

- `morse_decode` 查询点划对应字符。
- `morse_begin` / `morse_end` 记录点划及实际信号结束时刻。
- `morse_advance` 根据静默提交字符/词，返回 `false` 表示历史内存分配失败。
- `morse_history_lines` 按调用方指定的行列数分页，不包含输入中的字符。

使用 `morse_t model = {0};` 初始化，每个实例由单个调用方串行管理。
历史在 RAM 中动态增长；实例结束使用时释放 `model.history`，持有历史后避免浅拷贝实例。
显示坐标和当前视口尺寸由显示组件提供，本组件只处理文字行列布局。

主机验证：在项目根目录运行 `python3 tools/test_host.py`。
