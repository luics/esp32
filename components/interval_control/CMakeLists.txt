idf_component_register(SRCS "interval_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_adc freertos log)
