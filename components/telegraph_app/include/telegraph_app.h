#pragma once

/* 在 app_main 任务中调用一次；初始化外设后持续处理输入，不返回。
 * 组件独占声光输出与应用状态，发生初始化/运行错误时沿用 ESP_ERROR_CHECK。 */
void telegraph_app_run(void);
