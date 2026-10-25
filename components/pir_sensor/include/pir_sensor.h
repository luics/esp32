#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct pir_sensor *pir_sensor_handle_t;
typedef struct {
    bool motion;
    bool changed;
    uint32_t active_ms;
} pir_sensor_sample_t;

/* 高电平表示运动；调用方定期采样，组件仅记录电平变化。 */
esp_err_t pir_sensor_create(int gpio, pir_sensor_handle_t *out_handle);
esp_err_t pir_sensor_sample(pir_sensor_handle_t handle, uint64_t now_ms,
                            pir_sensor_sample_t *out_sample);
esp_err_t pir_sensor_delete(pir_sensor_handle_t handle);
