set(display_sources "display.c")
if(NOT CONFIG_DISPLAY_WOKWI_SH1107)
    list(APPEND display_sources "ssd1327.c")
endif()

idf_component_register(SRCS ${display_sources}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_lcd esp_lcd_sh1107 freertos heap log)
