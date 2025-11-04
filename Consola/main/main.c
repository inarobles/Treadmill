/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bsp/esp32_p4_function_ev_board.h"
#include "lvgl.h"
#include "treadmill_state.h"
#include "audio.h"
#include "ui.h"
#include "button_handler.h"
#include "ble_client.h"
#include "wifi_client.h" // <-- FIX: Added missing include
#include "wifi_manager.h"
#include "esp_hosted.h"
#include "cm_master.h"  // CM Protocol Master

static const char *TAG = "MainApp";

// Global variables are defined in treadmill_state.c

void app_main(void) {
    // Suppress verbose logs from specific components
    esp_log_level_set("i2s_common", ESP_LOG_ERROR); // Suppress I2S warnings
    esp_log_level_set("cache", ESP_LOG_NONE);        // Completely suppress cache msync errors (known issue with ESP32-P4 PSRAM)

    // Initialize NVS - required for WiFi and Bluetooth
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize ESP-Hosted transport layer
    // This must be done before initializing WiFi or BLE clients
    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_init() failed with error: %d", ret);
        return;
    }

    // Initialize WiFi Manager for network scanning and credential management
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "wifi_manager_init() failed with error: %d (continuando...)", ret);
    }

    g_state_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == NULL) {
        ESP_LOGE(TAG, "Fallo al crear el mutex de estado. Abortando.");
        abort();
    }

    // Initialize display with custom configuration
    // IMPORTANT: buff_spiram = true with buff_dma = false avoids cache sync errors
    // DMA + SPIRAM is not supported, but SPIRAM alone works fine for MIPI-DSI
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,    // DMA not compatible with SPIRAM buffers
            .buff_spiram = true,  // Use PSRAM for DMA buffers - required for proper cache coherency
            .sw_rotate = true,
        }
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();
    bsp_display_rotate(NULL, LV_DISP_ROT_270);
    
    // Initialize UI
    ui_init();

    // Initialize Audio
    audio_init();

    // Initialize Button Handler
    button_handler_init();

    // Create UI update task
    xTaskCreate(ui_update_task, "ui_update_task", 8192, NULL, 5, NULL);

    // Initialize BLE Client for Heart Rate Monitor
    ble_client_init();

    // Initialize WiFi Client for scanning
    wifi_client_init();

    // Initialize CM Protocol Master for RS485 communication
    ret = cm_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cm_master_init() failed with error: %d", ret);
        // Continue anyway - not critical for UI functionality
    } else {
        // Start master task
        ret = cm_master_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "cm_master_start() failed with error: %d", ret);
        }
    }

    ESP_LOGI(TAG, "Inicialización completa.");
}
