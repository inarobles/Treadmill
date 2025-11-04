idf_component_register(SRCS "main.c"
                      "treadmill_state.c"
                      "audio.c"
                      "ui.c"
                      "ui_wifi.c"
                      "button_handler.c"
                      "ble_client.c"
                      "wifi_client.c"
                      "wifi_manager.c"
                      "cm_master.c"
                      "fonts/chivo_mono_100.c"
                      "fonts/chivo_mono_70.c"
                INCLUDE_DIRS "."
                PRIV_REQUIRES nvs_flash
                              bt
                              esp_hosted
                              esp_wifi_remote
                              esp_http_client
                              esp-tls
                              bsp_extra
                              esp32_p4_function_ev_board
                              esp_lcd_touch_gsl3680
                              esp_lcd_jd9365
                              cm_protocol
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
