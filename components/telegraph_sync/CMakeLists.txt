idf_component_register(SRCS "telegraph_sync.c" "sync_dp.c" "sync_dp_response.c" "sync_outbox.c" "sync_http.c" "sync_tls_timing.c" "sync_verify_cache.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common morse
                       PRIV_REQUIRES esp_http_client cjson mbedtls esp_wifi esp_event esp_netif
                                     nvs_flash freertos log esp_timer
                       EMBED_TXTFILES "telegraph_iot_ca.crt")

# TLS 初始化保留 P-256 配置，握手/验签包装用于分段观测；均调用原函数，不修改 SDK。
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=mbedtls_ssl_handshake_client_step"
                       "-Wl,--wrap=psa_verify_hash" "-Wl,--wrap=mbedtls_ssl_setup")
