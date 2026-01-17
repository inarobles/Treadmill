/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/uart.h" // <-- FIX: Added missing include for UART functions
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
#include "ia_sync.h"
#include "ia_telemetry.h"
#include "acoustic_service.h"
#include "slave_ota.h"


static const char *TAG = "MainApp";

/**
 * Delayed BLE initialization task.
 * Waits for the ESP-Hosted transport to fully stabilize before initializing NimBLE.
 * This prevents HCI timeout errors caused by sending commands to the C6 too early.
 */
static void delayed_ble_init_task(void *pvParameters) {
    ESP_LOGI(TAG, "[BLE DELAY] Waiting 15 seconds for transport to fully stabilize...");
    vTaskDelay(pdMS_TO_TICKS(15000));  // Increased to 15 seconds to test timing hypothesis
    
    ESP_LOGI(TAG, "[BLE DELAY] Starting BLE Client initialization NOW");
    ble_client_init();
    ESP_LOGI(TAG, "[BLE DELAY] BLE Client init complete");
    
    vTaskDelete(NULL);
}

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
    
    // Check and update co-processor if version is 0.0.0
    check_and_update_slave_if_needed();

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
    // Initialize CM Protocol Master early to ensure mutex is ready for button events
    cm_master_init();
    
    // Initialize UI
    ui_init();

    // Initialize Audio
    audio_init();

    // Initialize Button Handler
    button_handler_init();

    // Create UI update task
    xTaskCreate(ui_update_task, "ui_update_task", 8192, NULL, 5, NULL);



    // Initialize BLE Client for Heart Rate Monitor (delayed to avoid HCI timeout)
    // BLE init moved to delayed task to allow C6 transport to stabilize
    xTaskCreate(delayed_ble_init_task, "delayed_ble", 4096, NULL, 5, NULL);

    // Initialize WiFi Client for scanning
    wifi_client_init();

    // Configurar UART para comunicación con la Base (ASCII)
    ESP_LOGI(TAG, "Configurando UART para comunicación con Base...");
    uart_config_t uart_config = {
        .baud_rate = CM_MASTER_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(CM_MASTER_UART_PORT, 512 * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CM_MASTER_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CM_MASTER_UART_PORT,
                                  CM_MASTER_TX_PIN,
                                  CM_MASTER_RX_PIN,
                                  UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE));

    // Initialize CM Protocol Master for RS485 communication
    // La función cm_master_init() fue eliminada. Ahora solo iniciamos las tareas.
    ret = cm_master_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cm_master_start() failed with error: %d", ret);
    }

    // Reinforce log suppression for cache before sync starts
    esp_log_level_set("cache", ESP_LOG_NONE);
    // Initialize IA Sync Client
    ret = ia_sync_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ia_sync_init() failed with error: %d", ret);
    }

    // Initialize IA Telemetry Module
    ret = ia_telemetry_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ia_telemetry_init() failed with error: %d", ret);
    }

    // Initialize Acoustic Podometer Service (Microphone)
    ret = acoustic_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "acoustic_service_init() failed with error: %d", ret);
    }


    ESP_LOGI(TAG, "Inicialización completa.");
}
