idf_component_register(SRCS "telegraph_app.c" "dp_prompt.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES morse telegraph telegraph_sync telegraph_ha display beep blink button_input interval_control pir_sensor
                                     freertos log esp_timer esp_system esp_driver_gpio)
