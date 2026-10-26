idf_component_register(SRCS "telegraph_ha.c" "ha_protocol.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES mqtt cjson telegraph_sync esp_hw_support mbedtls freertos log)
