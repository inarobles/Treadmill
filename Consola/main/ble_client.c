/*
 * BLE Client for Heart Rate Monitor using NimBLE on ESP-Hosted Architecture
 *
 * Refactored to support UI-driven scanning, device selection, and persistence.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdbool.h>

// Core NimBLE includes
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Key include for the hosted architecture
#include "esp_hosted.h"

#include "treadmill_state.h"
#include "ble_client.h"

static const char *TAG = "NIMBLE_BLE_CLIENT";

// NVS constants for storing the BLE device address
#define NVS_NAMESPACE "ble_client"
#define NVS_KEY_SAVED_ADDR "saved_addr"

// Heart Rate Service and Characteristic UUIDs
static const ble_uuid16_t g_svc_heart_rate_uuid = BLE_UUID16_INIT(0x180D);
static const ble_uuid16_t g_chr_heart_rate_meas_uuid = BLE_UUID16_INIT(0x2A37);

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_hr_chr_val_handle = 0;
static uint8_t g_own_addr_type;

// --- NEW: Globals for scanning and device list ---
#define MAX_DISCOVERED_DEVICES 20
static ble_addr_t g_discovered_devices[MAX_DISCOVERED_DEVICES];
static int g_discovered_device_count = 0;
static ble_device_found_callback_t g_device_found_cb = NULL;
static bool g_is_scanning = false;
static bool g_user_initiated_disconnect = false;  // Flag to prevent auto-reconnect after manual scan
static TaskHandle_t g_reconnect_task_handle = NULL;
static SemaphoreHandle_t g_ble_state_mutex = NULL;  // Protects g_is_scanning and g_user_initiated_disconnect


// Forward declarations
static void ble_reconnect_task(void *pvParameters);
static void ble_client_scan_internal(void);
static int ble_client_gap_event(struct ble_gap_event *event, void *arg);
static void ble_client_on_sync(void);
static void ble_client_on_reset(int reason);
static void ble_host_task(void *param);
static int ble_client_on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg);
static int ble_client_on_char_disc(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg);
static int ble_client_on_service_disc(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg);

// --- NEW/MODIFIED PUBLIC FUNCTIONS ---

/**
 * @brief Starts a new BLE scan for devices advertising the Heart Rate service.
 */
void ble_client_start_scan(ble_device_found_callback_t cb) {
    bool is_scanning = false;
    if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        is_scanning = g_is_scanning;
        xSemaphoreGive(g_ble_state_mutex);
    }

    if (is_scanning) {
        ESP_LOGW(TAG, "Scan already in progress.");
        return;
    }

    // If already connected, disconnect first to allow scanning
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "Disconnecting from current device to start scan...");
        if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_user_initiated_disconnect = true;  // Mark as user-initiated to prevent auto-reconnect
            xSemaphoreGive(g_ble_state_mutex);
        }
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_hr_chr_val_handle = 0;
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_treadmill_state.ble_connected = false;
            g_treadmill_state.real_pulse = 0;
            xSemaphoreGive(g_state_mutex);
        }
        // Wait a bit for disconnection to complete
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Starting new scan.");
    g_device_found_cb = cb;
    g_discovered_device_count = 0;
    memset(g_discovered_devices, 0, sizeof(g_discovered_devices));
    ble_client_scan_internal();
}

/**
 * @brief Connects to a specific BLE device using its address.
 */
void ble_client_connect(ble_addr_t addr) {
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Already connected or connecting.");
        return;
    }

    // Stop scanning before connecting
    bool was_scanning = false;
    if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        was_scanning = g_is_scanning;
        xSemaphoreGive(g_ble_state_mutex);
    }

    if (was_scanning) {
        int rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGE(TAG, "Failed to cancel scan before connecting; rc=%d", rc);
        }
        if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_is_scanning = false;
            xSemaphoreGive(g_ble_state_mutex);
        }
    }

    ESP_LOGI(TAG, "Attempting to connect to device with address: %02x:%02x:%02x:%02x:%02x:%02x",
             addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1], addr.val[0]);

    // Reset user disconnect flag when manually connecting to a new device
    if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_user_initiated_disconnect = false;
        xSemaphoreGive(g_ble_state_mutex);
    }

    int rc = ble_gap_connect(g_own_addr_type, &addr, 30000, NULL, ble_client_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initiate connect; rc=%d. Restarting scan.", rc);
        ble_client_scan_internal();
    }
}

/**
 * @brief Saves the address of a BLE device to Non-Volatile Storage (NVS).
 */
void ble_client_save_device(ble_addr_t addr) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY_SAVED_ADDR, &addr, sizeof(ble_addr_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write address to NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Saved device address to NVS.");
        nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
}

/**
 * @brief Loads a previously saved BLE device address from NVS.
 */
bool ble_client_load_saved_device(ble_addr_t *addr) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, no device saved yet.");
        return false;
    }

    size_t required_size = sizeof(ble_addr_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_SAVED_ADDR, addr, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read saved address from NVS: %s", esp_err_to_name(err));
        return false;
    }

    if (required_size != sizeof(ble_addr_t)) {
        ESP_LOGW(TAG, "NVS blob size mismatch, ignoring saved address.");
        return false;
    }
    
    ESP_LOGI(TAG, "Successfully loaded saved device address from NVS.");
    return true;
}


// --- INTERNAL FUNCTIONS ---

/**
 * Main GAP event handler.
 */
static int ble_client_gap_event(struct ble_gap_event *event, void *arg) {
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {
    // Event: Discovery (Advertisement received)
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        // Check if the advertisement contains the Heart Rate Service UUID
        bool hr_service_found = false;
        for (int i = 0; i < fields.num_uuids16; i++) {
            if (ble_uuid_cmp(&fields.uuids16[i].u, &g_svc_heart_rate_uuid.u) == 0) {
                hr_service_found = true;
                break;
            }
        }

        if (hr_service_found) {
            // Check if we've already seen this device
            for (int i = 0; i < g_discovered_device_count; i++) {
                if (ble_addr_cmp(&event->disc.addr, &g_discovered_devices[i]) == 0) {
                    return 0; // Already discovered, ignore.
                }
            }

            // Add to list of discovered devices
            if (g_discovered_device_count < MAX_DISCOVERED_DEVICES) {
                g_discovered_devices[g_discovered_device_count++] = event->disc.addr;

                // Extract device name
                char dev_name[30] = {0};
                if (fields.name != NULL && fields.name_len > 0) {
                    int name_len = fields.name_len > sizeof(dev_name) - 1 ? sizeof(dev_name) - 1 : fields.name_len;
                    memcpy(dev_name, fields.name, name_len);
                } else {
                    snprintf(dev_name, sizeof(dev_name), "HRM-%02x%02x", event->disc.addr.val[1], event->disc.addr.val[0]);
                }

                ESP_LOGI(TAG, "Found Heart Rate device: %s", dev_name);

                // Notify UI via callback
                if (g_device_found_cb) {
                    g_device_found_cb(dev_name, event->disc.addr);
                }
            }
        }
        return 0;

    // Event: Connection established or failed
    case BLE_GAP_EVENT_CONNECT:
        g_is_scanning = false; // Scan is stopped on connection attempt
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Connection established; conn_handle=%d", event->connect.conn_handle);
            g_conn_handle = event->connect.conn_handle;

            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_treadmill_state.ble_connected = true;
                xSemaphoreGive(g_state_mutex);
            }
            
            // Save the successfully connected device for next time
            struct ble_gap_conn_desc desc;
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                ble_client_save_device(desc.peer_id_addr);
            }

            // Discover all services
            rc = ble_gattc_disc_all_svcs(g_conn_handle, ble_client_on_service_disc, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to discover services; rc=%d", rc);
            }
        } else {
            ESP_LOGE(TAG, "Connection attempt failed; status=%d. User can re-scan.", event->connect.status);
            // Do not automatically restart scan. Let the user decide.
        }
        return 0;

    // Event: Disconnected
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_hr_chr_val_handle = 0;

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_treadmill_state.ble_connected = false;
            g_treadmill_state.real_pulse = 0;
            xSemaphoreGive(g_state_mutex);
        }

        // Auto-reconnect to saved device after disconnect (unless user initiated scan)
        if (!g_user_initiated_disconnect) {
            ble_addr_t saved_addr;
            if (ble_client_load_saved_device(&saved_addr)) {
                ESP_LOGI(TAG, "Attempting to reconnect to saved device...");
                // Wait a bit before reconnecting to avoid connection spam
                vTaskDelay(pdMS_TO_TICKS(2000));
                ble_client_connect(saved_addr);
            } else {
                ESP_LOGI(TAG, "No saved device to reconnect to.");
            }
        } else {
            ESP_LOGI(TAG, "User initiated disconnect - not auto-reconnecting.");
            g_user_initiated_disconnect = false;  // Reset flag
        }
        return 0;

    // Event: Scan complete
    case BLE_GAP_EVENT_DISC_COMPLETE:
        g_is_scanning = false;
        ESP_LOGI(TAG, "Scan complete; reason=%d", event->disc_complete.reason);
        // Can add a UI callback here if needed to hide a "Scanning..." message
        return 0;

    // Event: Notification received
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == g_hr_chr_val_handle) {
            uint8_t *data = event->notify_rx.om->om_data;
            uint16_t len = event->notify_rx.om->om_len;

            if (len >= 2) {
                uint8_t flags = data[0];
                uint16_t bpm = (flags & 0x01) ? ((data[2] << 8) | data[1]) : data[1];

                if (bpm > 30 && bpm < 250) { // Basic sanity check
                    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        g_treadmill_state.real_pulse = bpm;
                        xSemaphoreGive(g_state_mutex);
                    }
                }
            }
        }
        return 0;

    default:
        return 0;
    }
}

/**
 * Callback for service discovery.
 */
static int ble_client_on_service_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                    const struct ble_gatt_svc *service, void *arg) {
    if (error->status == 0 && service != NULL) {
        if (ble_uuid_cmp(&service->uuid.u, &g_svc_heart_rate_uuid.u) == 0) {
            ESP_LOGI(TAG, "Found Heart Rate Service. Discovering characteristics...");
            ble_gattc_disc_all_chrs(conn_handle, service->start_handle, service->end_handle, ble_client_on_char_disc, NULL);
        }
    }
    return 0;
}

/**
 * Callback for characteristic discovery.
 */
static int ble_client_on_char_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                 const struct ble_gatt_chr *chr, void *arg) {
    if (error->status == 0 && chr != NULL) {
        if (ble_uuid_cmp(&chr->uuid.u, &g_chr_heart_rate_meas_uuid.u) == 0) {
            ESP_LOGI(TAG, "Found Heart Rate Measurement characteristic.");
            g_hr_chr_val_handle = chr->val_handle;
            ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, 0xffff, ble_client_on_dsc_disc, NULL);
        }
    }
    return 0;
}

/**
 * Callback for descriptor discovery.
 */
static int ble_client_on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    if (error->status == 0 && dsc != NULL) {
        if (dsc->uuid.u.type == BLE_UUID_TYPE_16 && ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            ESP_LOGI(TAG, "Found CCCD for HR Measurement. Enabling notifications.");
            uint8_t value[2] = {0x01, 0x00};
            ble_gattc_write_flat(conn_handle, dsc->handle, value, sizeof(value), NULL, NULL);
        }
    }
    return 0;
}

/**
 * Task that periodically attempts to reconnect to saved device when not connected.
 */
static void ble_reconnect_task(void *pvParameters) {
    ble_addr_t saved_addr;
    bool has_saved_device = false;

    ESP_LOGI(TAG, "Reconnect task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds

        // Skip if scanning or user initiated disconnect (protected access)
        bool should_skip = false;
        if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            should_skip = g_is_scanning || g_user_initiated_disconnect;
            xSemaphoreGive(g_ble_state_mutex);
        }
        if (should_skip) {
            continue;
        }

        // Skip if already connected
        if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            continue;
        }

        // Load saved device (only once or when needed)
        if (!has_saved_device) {
            has_saved_device = ble_client_load_saved_device(&saved_addr);
            if (!has_saved_device) {
                continue;  // No saved device, keep checking
            }
        }

        // Try to reconnect
        ESP_LOGI(TAG, "Not connected - attempting reconnect to saved device...");
        ble_client_connect(saved_addr);
    }
}

/**
 * Callback for when the BLE host has successfully synced with the controller.
 */
static void ble_client_on_sync(void) {
    int rc;
    ESP_LOGI(TAG, "BLE Host synced.");

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    // --- NEW: Auto-connect to saved device ---
    ble_addr_t saved_addr;
    if (ble_client_load_saved_device(&saved_addr)) {
        ble_client_connect(saved_addr);
    } else {
        ESP_LOGI(TAG, "No saved device found. Waiting for user to initiate scan.");
    }

    // Start reconnect task if not already running
    if (g_reconnect_task_handle == NULL) {
        xTaskCreate(ble_reconnect_task, "ble_reconnect", 4096, NULL, 5, &g_reconnect_task_handle);
        ESP_LOGI(TAG, "BLE reconnect task started");
    }
}

/**
 * Callback for when the BLE host resets.
 */
static void ble_client_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

/**
 * Initiates a BLE scan.
 */
static void ble_client_scan_internal(void) {
    struct ble_gap_disc_params disc_params;
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0; // Active scan
    disc_params.itvl = 0;    // Default interval
    disc_params.window = 0;  // Default window
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    // Scan for 10 seconds
    int rc = ble_gap_disc(g_own_addr_type, 10000, &disc_params, ble_client_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting scan; rc=%d", rc);
        g_is_scanning = false;
    } else {
        g_is_scanning = true;
        ESP_LOGI(TAG, "Starting BLE scan for Heart Rate sensors...");
    }
}

/**
 * Main BLE host task.
 */
void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * Initializes the BLE client.
 */
void ble_client_init(void) {
    ESP_LOGI(TAG, "Initializing BLE Client for ESP-Hosted...");

    // NOTE: NVS and esp_hosted are already initialized in main.c
    // Removed duplicate initialization to avoid errors

    // Initialize BLE state mutex
    if (g_ble_state_mutex == NULL) {
        g_ble_state_mutex = xSemaphoreCreateMutex();
        if (g_ble_state_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create BLE state mutex");
            return;
        }
    }

    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_client_on_sync;
    ble_hs_cfg.reset_cb = ble_client_on_reset;
    
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 0;

    const char *device_name = "p4-treadmill-console";
    ble_svc_gap_device_name_set(device_name);

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Client initialization complete.");
}