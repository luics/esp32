idf_component_register(SRCS "volume_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES beep
                       PRIV_REQUIRES esp_adc freertos log)
