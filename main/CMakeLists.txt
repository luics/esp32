idf_component_register(SRCS "app_main.c" "morse.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES display beep blink button_input interval_control freertos log esp_timer esp_system esp_driver_gpio)
