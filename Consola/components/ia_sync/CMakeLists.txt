idf_component_register(SRCS "ia_sync.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_http_client esp-tls mbedtls json esp_system)
