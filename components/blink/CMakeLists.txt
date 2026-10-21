idf_component_register(SRCS "blink.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_gpio freertos log led_strip)
