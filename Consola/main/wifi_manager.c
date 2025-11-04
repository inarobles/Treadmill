#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "WIFI_MANAGER";
static const char *NVS_NAMESPACE = "wifi_creds";

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Manager");
    // NVS is already initialized in main.c, just verify we can open our namespace
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi Manager initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_manager_scan_networks(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found)
{
    if (!networks || !num_found || max_networks == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Found %d access points", ap_count);

    if (ap_count == 0) {
        *num_found = 0;
        return ESP_OK;
    }

    // Allocate memory for all APs
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        return ESP_ERR_NO_MEM;
    }

    uint16_t ap_count_actual = ap_count;
    err = esp_wifi_scan_get_ap_records(&ap_count_actual, ap_records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
        free(ap_records);
        return err;
    }

    // Copy to output array, up to max_networks
    uint16_t count = (ap_count_actual < max_networks) ? ap_count_actual : max_networks;
    for (uint16_t i = 0; i < count; i++) {
        strncpy(networks[i].ssid, (char *)ap_records[i].ssid, WIFI_MANAGER_MAX_SSID_LEN - 1);
        networks[i].ssid[WIFI_MANAGER_MAX_SSID_LEN - 1] = '\0';
        networks[i].rssi = ap_records[i].rssi;
        networks[i].auth_mode = ap_records[i].authmode;
    }

    *num_found = count;
    free(ap_records);

    ESP_LOGI(TAG, "Returning %d networks", count);
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Saving credentials for SSID: %s", ssid);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Save password using SSID as key
    err = nvs_set_str(nvs_handle, ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Credentials saved successfully");
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_manager_load_credentials(const char *ssid, char *password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = WIFI_MANAGER_MAX_PASSWORD_LEN;
    err = nvs_get_str(nvs_handle, ssid, password, &required_size);

    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Credentials loaded for SSID: %s", ssid);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No credentials found for SSID: %s", ssid);
    } else {
        ESP_LOGE(TAG, "Failed to load credentials: %s", esp_err_to_name(err));
    }

    return err;
}

bool wifi_manager_has_credentials(const char *ssid)
{
    if (!ssid) {
        return false;
    }

    char password[WIFI_MANAGER_MAX_PASSWORD_LEN];
    esp_err_t err = wifi_manager_load_credentials(ssid, password);
    return (err == ESP_OK);
}

esp_err_t wifi_manager_delete_credentials(const char *ssid)
{
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Deleting credentials for SSID: %s", ssid);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(nvs_handle, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete credentials: %s", esp_err_to_name(err));
    } else {
        nvs_commit(nvs_handle);
        ESP_LOGI(TAG, "Credentials deleted successfully");
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_manager_get_current_ssid(char *ssid)
{
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK) {
        strncpy(ssid, (char *)ap_info.ssid, WIFI_MANAGER_MAX_SSID_LEN - 1);
        ssid[WIFI_MANAGER_MAX_SSID_LEN - 1] = '\0';
    }

    return err;
}

esp_err_t wifi_manager_get_saved_ssids(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found)
{
    if (!networks || !num_found || max_networks == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *num_found = 0;
    
    nvs_iterator_t iterator = NULL;
    esp_err_t res = nvs_entry_find(NVS_DEFAULT_PART_NAME, NVS_NAMESPACE, NVS_TYPE_ANY, &iterator);

    if (res == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved WiFi credentials found.");
        return ESP_OK;
    }
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "NVS find failed: %s", esp_err_to_name(res));
        return res;
    }

    uint16_t count = 0;
    while (iterator != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(iterator, &info);

        // Skip the order key
        if (strcmp(info.key, "ssid_order") == 0) {
            res = nvs_entry_next(&iterator);
            if (res != ESP_OK && res != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGE(TAG, "NVS next failed: %s", esp_err_to_name(res));
                break;
            }
            continue;
        }

        if(count < max_networks) {
            strncpy(networks[count].ssid, info.key, WIFI_MANAGER_MAX_SSID_LEN - 1);
            networks[count].ssid[WIFI_MANAGER_MAX_SSID_LEN - 1] = '\0';
            networks[count].rssi = 0;
            networks[count].auth_mode = WIFI_AUTH_OPEN;
            count++;
        }

        res = nvs_entry_next(&iterator);
        if (res != ESP_OK && res != ESP_ERR_NVS_NOT_FOUND) {
             ESP_LOGE(TAG, "NVS next failed: %s", esp_err_to_name(res));
             break;
        }
    }

    *num_found = count;
    nvs_release_iterator(iterator);

    ESP_LOGI(TAG, "Found %d saved SSIDs", count);
    return ESP_OK;
}

// --- Funciones para el orden de prioridad de redes ---

#define SSID_ORDER_KEY "ssid_order"

esp_err_t wifi_manager_set_last_connected(const char *ssid) {
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // 1. Leer el orden actual
    char* order_str = calloc(WIFI_MANAGER_MAX_NETWORKS, WIFI_MANAGER_MAX_SSID_LEN + 1);
    if (!order_str) {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    size_t required_size = WIFI_MANAGER_MAX_NETWORKS * (WIFI_MANAGER_MAX_SSID_LEN + 1);
    err = nvs_get_str(nvs_handle, SSID_ORDER_KEY, order_str, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        free(order_str);
        nvs_close(nvs_handle);
        return err;
    }

    // 2. Crear el nuevo orden
    char* new_order_str = calloc(WIFI_MANAGER_MAX_NETWORKS, WIFI_MANAGER_MAX_SSID_LEN + 1);
    if (!new_order_str) {
        free(order_str);
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    snprintf(new_order_str, WIFI_MANAGER_MAX_SSID_LEN + 1, "%s", ssid);

    char *temp_order_str = strdup(order_str);
    char *token = strtok(temp_order_str, ",");
    while (token != NULL) {
        if (strcmp(token, ssid) != 0) {
            if (strlen(new_order_str) > 0) {
                strncat(new_order_str, ",", required_size - strlen(new_order_str) - 1);
            }
            strncat(new_order_str, token, required_size - strlen(new_order_str) - 1);
        }
        token = strtok(NULL, ",");
    }
    free(temp_order_str);

    // 3. Guardar el nuevo orden
    err = nvs_set_str(nvs_handle, SSID_ORDER_KEY, new_order_str);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    ESP_LOGI(TAG, "Updated SSID order: %s", new_order_str);
    
    free(order_str);
    free(new_order_str);
    nvs_close(nvs_handle);
    
    return err;
}

esp_err_t wifi_manager_get_saved_ssids_ordered(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found) {
    if (!networks || !num_found || max_networks == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *num_found = 0;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    char* order_str = calloc(WIFI_MANAGER_MAX_NETWORKS, WIFI_MANAGER_MAX_SSID_LEN + 1);
     if (!order_str) {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    size_t required_size = WIFI_MANAGER_MAX_NETWORKS * (WIFI_MANAGER_MAX_SSID_LEN + 1);
    err = nvs_get_str(nvs_handle, SSID_ORDER_KEY, order_str, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_ERR_NVS_NOT_FOUND || strlen(order_str) == 0) {
        ESP_LOGI(TAG, "SSID order not found, falling back to unordered list");
        free(order_str);
        return wifi_manager_get_saved_ssids(networks, max_networks, num_found);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SSID order: %s", esp_err_to_name(err));
        free(order_str);
        return err;
    }

    uint16_t count = 0;
    char *token = strtok(order_str, ",");
    while (token != NULL && count < max_networks) {
        strncpy(networks[count].ssid, token, WIFI_MANAGER_MAX_SSID_LEN - 1);
        networks[count].ssid[WIFI_MANAGER_MAX_SSID_LEN - 1] = '\0';
        count++;
        token = strtok(NULL, ",");
    }

    *num_found = count;
    free(order_str);
    ESP_LOGI(TAG, "Found %d saved SSIDs in order", count);
    return ESP_OK;
}