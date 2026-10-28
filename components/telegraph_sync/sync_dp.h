#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t sync_dp_start(void);
bool sync_dp_transport_ready(void);
