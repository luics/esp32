#include "blink.h"
#include "esp_err.h"
#include "sdkconfig.h"

void app_main(void)
{
    ESP_ERROR_CHECK(blink_run(CONFIG_BLINK_GPIO));
}
