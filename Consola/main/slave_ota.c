#include "slave_ota.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_hosted_ota.h"
#include "esp_hosted.h"
#include "esp_system.h"

static const char *TAG = "SlaveOTA";

#define CHUNK_SIZE 1500

esp_err_t slave_ota_perform_from_partition(const char* partition_label) {
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partition_label);
    if (!partition) {
        ESP_LOGE(TAG, "Partition %s not found", partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Starting slave OTA from partition: %s (size: %u)", partition_label, (unsigned int)partition->size);

    esp_err_t ret = esp_hosted_slave_ota_begin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_slave_ota_begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t *buffer = malloc(CHUNK_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for OTA");
        esp_hosted_slave_ota_end();
        return ESP_ERR_NO_MEM;
    }

    uint32_t offset = 0;
    uint32_t total_sent = 0;
    while (offset < partition->size) {
        size_t to_read = (partition->size - offset > CHUNK_SIZE) ? CHUNK_SIZE : (partition->size - offset);
        ret = esp_partition_read(partition, offset, buffer, to_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Partition read failed at offset %u", (unsigned int)offset);
            break;
        }

        ret = esp_hosted_slave_ota_write(buffer, to_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(ret));
            break;
        }

        offset += to_read;
        total_sent += to_read;
        
        if (total_sent % (CHUNK_SIZE * 50) == 0) {
            ESP_LOGI(TAG, "Progress: %u bytes sent", (unsigned int)total_sent);
        }
        vTaskDelay(1); // Yield to other tasks
    }

    free(buffer);
    esp_hosted_slave_ota_end();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Slave OTA completed successfully. Activation initiated.");
        esp_hosted_slave_ota_activate();
        ESP_LOGI(TAG, "Waiting for C6 to reboot...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "Restarting Host to complete sync...");
        esp_restart();
    }

    return ret;
}

static void slave_ota_task(void *pvParameters) {
    esp_hosted_coprocessor_fwver_t ver_info = {0};
    esp_err_t ret;
    int retry = 0;
    
    ESP_LOGI(TAG, "Slave OTA task started. Waiting 7s for stability...");
    vTaskDelay(pdMS_TO_TICKS(7000));

    ESP_LOGI(TAG, "Waiting for ESP-Hosted transport to be ready...");
    while (retry < 200) { // 20 seconds timeout
        ESP_LOGD(TAG, "Checking transport status (retry %d)...", retry);
        ret = esp_hosted_get_coprocessor_fwversion(&ver_info);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Transport ready. Slave version: %u.%u.%u", 
                     (unsigned int)ver_info.major1, (unsigned int)ver_info.minor1, (unsigned int)ver_info.patch1);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        retry++;
    }

    if (ret == ESP_OK) {
        if (ver_info.major1 == 0 && ver_info.minor1 == 0 && ver_info.patch1 == 0) {
            ESP_LOGW(TAG, "Co-processor reporting 0.0.0. Triggering mandatory update.");
            slave_ota_perform_from_partition("slave_fw");
        } else if (ver_info.major1 < 2 || (ver_info.major1 == 2 && ver_info.minor1 < 8)) {
            ESP_LOGW(TAG, "Co-processor version is old (%u.%u.%u). Updating to target 2.8.3.",
                     (unsigned int)ver_info.major1, (unsigned int)ver_info.minor1, (unsigned int)ver_info.patch1);
            slave_ota_perform_from_partition("slave_fw");
        } else {
            ESP_LOGI(TAG, "Co-processor version is up to date.");
        }
    } else {
        ESP_LOGE(TAG, "Timed out waiting for transport. Cannot check co-processor version.");
    }

    ESP_LOGI(TAG, "Slave OTA task exiting.");
    vTaskDelete(NULL);
}

void check_and_update_slave_if_needed(void) {
    xTaskCreate(slave_ota_task, "slave_ota_task", 8192, NULL, 5, NULL);
}
