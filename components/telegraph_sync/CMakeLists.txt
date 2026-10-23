idf_component_register(SRCS "telegraph_sync.c" "sync_outbox.c" "sync_http.c" "sync_tls_timing.c" "sync_verify_cache.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common morse
                       PRIV_REQUIRES esp_http_client mbedtls esp_wifi esp_event esp_netif
                                     nvs_flash freertos log esp_timer)

# TLS 1.2 客户端阶段观测：保留原函数，避免修改 ESP-IDF 或开启全量 TLS debug。
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=mbedtls_ssl_handshake_client_step"
                       "-Wl,--wrap=psa_verify_hash")
