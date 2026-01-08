idf_component_register(SRCS "main.c"
                      "slave_ota.c"
                      "treadmill_state.c"
                      "audio.c"
                      "ui.c"
                      "ui_wifi.c"
                      "button_handler.c"
                      "ble_client.c"
                      "wifi_client.c"
                      "wifi_manager.c"
                      "cm_master.c"
                      "ia_telemetry.c"
                      "imu_service.c"
                      "icon_main.c"
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
                              ia_sync
                              esptool_py
                              esp_driver_i2c
)

# Custom flashing for ESP32-C6 slave firmware
# The binary is for C6, but we flash it to P4's flash (slave_fw partition)
# IMPORTANT: C6 firmware is already flashed and persistent in slave_fw partition
# The automatic OTA update system will update it if needed
# Uncomment the line below ONLY if you need to manually reflash the C6:
# esptool_py_flash_target_image(flash slave_fw 0xd10000 "${SLAVE_FW_BIN}")
set(SLAVE_FW_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../slave_fw/network_adapter_esp32c6.bin")
if(EXISTS "${SLAVE_FW_BIN}")
    # Commented out - C6 already flashed, use automatic OTA update instead
    # esptool_py_flash_target_image(flash slave_fw 0xd10000 "${SLAVE_FW_BIN}")
endif()

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
