#include "telegraph_app.h"

void app_main(void)
{
    /* 应用组件统一负责初始化与事件调度，复位后仍经过此入口。 */
    telegraph_app_run();
}
