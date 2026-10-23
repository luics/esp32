idf_component_register(SRCS "telegraph_app.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES morse telegraph telegraph_sync display beep blink button_input interval_control
                                     freertos log esp_timer esp_system esp_driver_gpio)
