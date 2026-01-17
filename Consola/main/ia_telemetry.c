#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "ia_telemetry.h"
#include "acoustic_service.h"
#include "treadmill_state.h"
#include "cm_master.h"

static const char *TAG = "IA_TELEMETRY";

// Max duration 30 minutes (900 samples)
// Each sample is ~25 chars: "12.5,15.0,180,180,1.25 | "
// 900 * 25 = 22,500 bytes. We use 256KB to be super safe.
#define TELEMETRY_BUFFER_SIZE (256 * 1024)

static char *s_telemetry_buffer = NULL;
static size_t s_current_pos = 0;
static bool s_is_recording = false;
static TaskHandle_t s_recording_task_hdl = NULL;

static void telemetry_task(void *pvParameters) {
    ESP_LOGI(TAG, "Recording task started (0.5 Hz)");
    
    // Header
    const char *header = "v,i,p,c,z; ";
    size_t header_len = strlen(header);
    memcpy(s_telemetry_buffer, header, header_len);
    s_current_pos = header_len;

    while (s_is_recording) {
        float speed = cm_master_get_real_speed();
        float incline = cm_master_get_current_incline();
        
        uint16_t pulse = 0;
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        if (g_treadmill_state.ble_connected) {
            pulse = g_treadmill_state.real_pulse;
        } else {
            pulse = g_treadmill_state.sim_pulse;
        }
        xSemaphoreGive(g_state_mutex);

        // Cadence from Acoustic Service
        float cadence = acoustic_service_get_cadence();
        
        // Calculate stride based on speed and cadence
        // stride (m) = (speed (km/h) / 3.6) / (cadence (spm) / 60)
        // stride (m) = (speed / cadence) * (60 / 3.6) = (speed / cadence) * 16.666
        float stride = (cadence > 20.0f) ? (speed / cadence) * 16.666f : 0.0f;


        char sample[64];
        // Format: "v,i,p,c,z"
        int len = snprintf(sample, sizeof(sample), "%.1f,%.1f,%u,%.0f,%.2f | ", 
                           speed, incline, pulse, cadence, stride);

        if (s_current_pos + len < TELEMETRY_BUFFER_SIZE - 1) {
            memcpy(s_telemetry_buffer + s_current_pos, sample, len);
            s_current_pos += len;
            s_telemetry_buffer[s_current_pos] = '\0';
        } else {
            ESP_LOGW(TAG, "Telemetry buffer full!");
            s_is_recording = false;
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // 0.5 Hz
    }

    ESP_LOGI(TAG, "Recording task stopped");
    vTaskDelete(NULL);
    s_recording_task_hdl = NULL;
}

esp_err_t ia_telemetry_init(void) {
    if (s_telemetry_buffer) return ESP_OK;

    // Allocate in PSRAM
    s_telemetry_buffer = (char *)heap_caps_malloc(TELEMETRY_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_telemetry_buffer) {
        ESP_LOGE(TAG, "Failed to allocate telemetry buffer in PSRAM");
        return ESP_ERR_NO_MEM;
    }
    memset(s_telemetry_buffer, 0, TELEMETRY_BUFFER_SIZE);
    ESP_LOGI(TAG, "Telemetry buffer allocated in PSRAM: %d KB", TELEMETRY_BUFFER_SIZE / 1024);
    return ESP_OK;
}

esp_err_t ia_telemetry_start_session(void) {
    if (s_is_recording) return ESP_ERR_INVALID_STATE;
    
    if (!s_telemetry_buffer) {
        esp_err_t err = ia_telemetry_init();
        if (err != ESP_OK) return err;
    }

    ia_telemetry_reset();
    s_is_recording = true;

    BaseType_t ret = xTaskCreate(telemetry_task, "telemetry_task", 4096, NULL, 4, &s_recording_task_hdl);
    if (ret != pdPASS) {
        s_is_recording = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void ia_telemetry_stop_session(void) {
    s_is_recording = false;
    // Task deletes itself when s_is_recording becomes false
}

const char* ia_telemetry_get_current_report(void) {
    return s_telemetry_buffer;
}

void ia_telemetry_reset(void) {
    if (s_telemetry_buffer) {
        memset(s_telemetry_buffer, 0, TELEMETRY_BUFFER_SIZE);
        s_current_pos = 0;
    }
}
