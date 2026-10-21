idf_component_register(SRCS "button_input.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES button esp_driver_gpio log)
