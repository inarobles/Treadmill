#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_heap_caps.h"

#include "wifi_client.h" // Include UI header to call ui_loading_complete
#include "lvgl.h"
#include "ui.h"
#include "wifi_manager.h"

static const char *TAG = "WIFI_CLIENT";
static const char *TAG_CONNECTIVITY = "WIFI_CONNECTIVITY";

// Event group to signal when we are connected
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// --- Variables for iterative connection logic ---
static wifi_network_info_t s_saved_networks[WIFI_MANAGER_MAX_NETWORKS];
static uint16_t s_num_saved_networks = 0;
static int s_connection_attempt_index = 0;

// URLs for uploading data via HTTP POST
#define UPLOAD_URL "http://entrenadorpersonalia.ct.ws/upload.php"
#define API_KEY    "Spoofer86"

// File identifiers for upload.php
#define FILE_INA    "entreno_cinta_ina.txt"
#define FILE_ITSASO "entreno_cinta_itsaso.txt"

// Pipedream URL for uploading (HTTPS with workaround for ESP32-P4 cache bug)
#define PIPEDREAM_URL "https://eo63vvlnoq57ke8.m.pipedream.net"

// URL for GET-based upload (workaround for InfinityFree anti-bot)
#define UPLOAD_GET_URL "http://entrenadorpersonalia.ct.ws/upload_get.php"

// URL for POST-based upload with advanced headers (to bypass InfinityFree anti-bot)
#define UPLOAD_POST_URL "http://entrenadorpersonalia.ct.ws/upload_post.php"

// Google Scripts URLs for uploading to Google Drive
#define GOOGLE_SCRIPT_INA    "https://script.google.com/macros/s/AKfycbxCjlHprXi40arHypxwlsWov-_zrejxzbOLiIhFZo7ffizBNK_z_oNG09kBk1qS5VJ-kw/exec"
#define GOOGLE_SCRIPT_ITSASO "https://script.google.com/macros/s/AKfycbxDA9al2_Yewn3ReoThMDZYYTrJNNoNTbKG6FV4upAWCRmUwjK9NGK5Ae9lZRb3taB_pw/exec"

// Current connection credentials
static char current_ssid[64] = "";
static char current_password[64] = "";

// --- WIFI STATUS ---
static bool g_wifi_connected = false;
bool g_internet_connected = false;

// --- SNTP STATUS ---
static bool g_sntp_initialized = false;

// --- DOWNLOAD GLOBALS (as expected by ui.c) ---
char *g_downloaded_file_content = NULL;
int g_downloaded_file_size = 0;
static int received_len = 0;
SemaphoreHandle_t g_download_mutex = NULL;  // Exported for ui.c access

// --- FORWARD DECLARATIONS ---
static void http_download_task(void *pvParameters);
static void upload_task(void *pvParameters);
static void google_script_upload_task(void *pvParameters);
static bool sync_time_sntp(void);
static void wifi_connect_task(void *pvParameters);
static void internet_check_task(void *pvParameters);

// --- HTTP EVENT HANDLER FOR CONNECTIVITY CHECK ---
esp_err_t _http_connectivity_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG_CONNECTIVITY, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG_CONNECTIVITY, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG_CONNECTIVITY, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG_CONNECTIVITY, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG_CONNECTIVITY, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

void check_internet_connectivity(void)
{
    esp_http_client_config_t config = {
        .url = "http://connectivitycheck.gstatic.com/generate_204",
        .event_handler = _http_connectivity_event_handler,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 204) {
            ESP_LOGI(TAG_CONNECTIVITY, "Internet connectivity confirmed.");
            g_internet_connected = true;
        } else {
            ESP_LOGW(TAG_CONNECTIVITY, "Internet check failed with status code: %d", status_code);
            g_internet_connected = false;
        }
    } else {
        ESP_LOGE(TAG_CONNECTIVITY, "Internet check failed: %s", esp_err_to_name(err));
        g_internet_connected = false;
    }
    esp_http_client_cleanup(client);
}

bool is_internet_connected(void) {
    return g_internet_connected;
}





static void internet_check_task(void *pvParameters) {
    check_internet_connectivity();
    vTaskDelete(NULL);
}

static void wifi_connect_task(void *pvParameters)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "wifi_connect_task: WiFi Connected");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "wifi_connect_task: WiFi Connection Failed");
    } else {
        ESP_LOGE(TAG, "wifi_connect_task: UNEXPECTED EVENT");
    }
    vTaskDelete(NULL);
}



static void try_next_saved_network(void) {
    if (s_connection_attempt_index < s_num_saved_networks) {
        ESP_LOGI(TAG, "Attempting to connect to %s (%d/%d)", 
                 s_saved_networks[s_connection_attempt_index].ssid, 
                 s_connection_attempt_index + 1, s_num_saved_networks);

        wifi_config_t wifi_config = {0};
        strlcpy((char *)wifi_config.sta.ssid, s_saved_networks[s_connection_attempt_index].ssid, sizeof(wifi_config.sta.ssid));
        
        char password[WIFI_MANAGER_MAX_PASSWORD_LEN];
        esp_err_t err = wifi_manager_load_credentials((const char*)wifi_config.sta.ssid, password);

        s_connection_attempt_index++; // Increment index for the next attempt

        if (err == ESP_OK) {
            strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Failed to load password for %s. Skipping.", (char*)wifi_config.sta.ssid);
            // Immediately try the next one
            try_next_saved_network();
        }
    } else {
        ESP_LOGI(TAG, "No more saved networks to try.");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        
        // Open WiFi selector screen
        ui_open_wifi_list();
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Initializing connection process.");
        wifi_manager_get_saved_ssids_ordered(s_saved_networks, WIFI_MANAGER_MAX_NETWORKS, &s_num_saved_networks);
        s_connection_attempt_index = 0;
        try_next_saved_network();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED: Connection failed or lost.");
        g_wifi_connected = false;
        g_internet_connected = false;

        // Try next network
        ESP_LOGI(TAG, "Trying next saved network...");
        try_next_saved_network();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        g_wifi_connected = true;

        // Reset connection attempts
        s_connection_attempt_index = 0;

        // Update the priority order
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            wifi_manager_set_last_connected((const char*)ap_info.ssid);
        }

        // Manually set DNS server to fix connectivity check
        esp_netif_dns_info_t dns_info;
        IP_ADDR4(&dns_info.ip, 8, 8, 8, 8); // Google's DNS
        esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        ESP_LOGI(TAG, "Manually set DNS server to 8.8.8.8");

        // Check internet connectivity in a separate task
        xTaskCreate(&internet_check_task, "internet_check_task", 4096, NULL, 5, NULL);
    }
}

esp_err_t wifi_client_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Client...");

    // Initialize download mutex
    if (g_download_mutex == NULL) {
        g_download_mutex = xSemaphoreCreateMutex();
        if (g_download_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create download mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    xTaskCreate(&wifi_connect_task, "wifi_connect_task", 4096, NULL, 5, NULL);

    return ESP_OK;
}

// --- HTTP EVENT HANDLER ---
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    // This handler is now only used for the download task.
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (g_downloaded_file_content) {
                    heap_caps_free(g_downloaded_file_content);
                    g_downloaded_file_content = NULL;
                }
                g_downloaded_file_size = 0;
                received_len = 0;
                xSemaphoreGive(g_download_mutex);
            }
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            if (g_downloaded_file_size == 0 && strcasecmp(evt->header_key, "Content-Length") == 0) {
                int content_length = atoi(evt->header_value);
                if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    g_downloaded_file_size = content_length;
                    if (g_downloaded_file_size > 0) {
                        g_downloaded_file_content = (char *) heap_caps_malloc(g_downloaded_file_size + 1, MALLOC_CAP_INTERNAL);
                        if (!g_downloaded_file_content) {
                            ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
                            xSemaphoreGive(g_download_mutex);
                            return ESP_FAIL;
                        }
                    }
                    xSemaphoreGive(g_download_mutex);
                }
            }
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                // Handle chunked encoding (no Content-Length header)
                if (!g_downloaded_file_content && evt->data_len > 0) {
                    // Allocate initial buffer (assume max 4KB for training files)
                    g_downloaded_file_size = 4096;
                    g_downloaded_file_content = (char *) heap_caps_malloc(g_downloaded_file_size + 1, MALLOC_CAP_INTERNAL);
                    if (!g_downloaded_file_content) {
                        ESP_LOGE(TAG, "Failed to allocate memory for chunked response");
                        xSemaphoreGive(g_download_mutex);
                        return ESP_FAIL;
                    }
                    ESP_LOGI(TAG, "Allocated buffer for chunked transfer");
                }

                // Expand buffer if needed
                if (g_downloaded_file_content && (received_len + evt->data_len > g_downloaded_file_size)) {
                    size_t new_size = received_len + evt->data_len + 1024; // Extra space
                    char *new_buffer = (char *) heap_caps_realloc(g_downloaded_file_content, new_size, MALLOC_CAP_INTERNAL);
                    if (!new_buffer) {
                        ESP_LOGE(TAG, "Failed to expand buffer");
                        xSemaphoreGive(g_download_mutex);
                        return ESP_FAIL;
                    }
                    g_downloaded_file_content = new_buffer;
                    g_downloaded_file_size = new_size - 1;
                    ESP_LOGI(TAG, "Expanded buffer to %d bytes", new_size);
                }

                // Copy data
                if (g_downloaded_file_content) {
                    memcpy(g_downloaded_file_content + received_len, evt->data, evt->data_len);
                    received_len += evt->data_len;
                }

                xSemaphoreGive(g_download_mutex);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (g_downloaded_file_content) {
                    g_downloaded_file_content[received_len] = '\0';
                    g_downloaded_file_size = received_len;
                    ESP_LOGI(TAG, "Download complete. Total received: %d bytes", received_len);
                }
                xSemaphoreGive(g_download_mutex);
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// --- SNTP TIME SYNCHRONIZATION ---
static bool sync_time_sntp(void)
{
    // SNTP should already be initialized when WiFi connected
    if (!g_sntp_initialized) {
        ESP_LOGW(TAG, "SNTP no inicializado. Inicializando ahora...");
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        g_sntp_initialized = true;
    }

    // Check if already synchronized
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year + 1900 >= 2024) {
        ESP_LOGI(TAG, "Hora ya sincronizada: %d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }

    ESP_LOGI(TAG, "Esperando sincronizacion SNTP...");

    // Wait for time synchronization with timeout (30 seconds total)
    int retry = 0;
    const int retry_count = 30;

    while (timeinfo.tm_year + 1900 < 2024 && retry < retry_count) {
        ESP_LOGI(TAG, "Esperando sincronizacion SNTP... (%d/%d)", retry + 1, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }

    if (timeinfo.tm_year + 1900 < 2024) {
        ESP_LOGE(TAG, "Timeout: No se pudo sincronizar SNTP despues de %d segundos", retry_count);
        return false;
    }

    ESP_LOGI(TAG, "Hora sincronizada exitosamente: %d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    return true;
}

// --- TASKS ---
static void http_download_task(void *pvParameters)
{
    if (pvParameters == NULL) {
        ESP_LOGE(TAG, "http_download_task received NULL parameter! Aborting download.");
        if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (g_downloaded_file_content) {
                heap_caps_free(g_downloaded_file_content);
                g_downloaded_file_content = NULL;
            }
            g_downloaded_file_size = 0;
            xSemaphoreGive(g_download_mutex);
        }
        ui_loading_complete();
        vTaskDelete(NULL);
        return;
    }
    char *url = (char*)pvParameters;
    const int max_wait_time_ms = 60000;
    const int check_interval_ms = 500;
    const int max_retries = 3;
    const int retry_delay_ms = 2000;
    int elapsed_time_ms = 0;
    bool download_started = false;
    bool download_success = false;

    ESP_LOGI(TAG, "Download task started. Waiting for WiFi connection...");

    while (elapsed_time_ms < max_wait_time_ms) {
        if (is_wifi_connected()) {
            ESP_LOGI(TAG, "WiFi is connected. Starting download from %s", url);
            download_started = true;

            // Retry loop for robustness
            for (int retry = 0; retry < max_retries && !download_success; retry++) {
                if (retry > 0) {
                    ESP_LOGW(TAG, "Retry attempt %d/%d after %dms delay...", retry + 1, max_retries, retry_delay_ms);
                    vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                }

                ESP_LOGI(TAG, "Free heap before download: %lu bytes", esp_get_free_heap_size());

                esp_http_client_config_t config = {
                    .url = url,
                    .event_handler = _http_event_handler,
                    .user_agent = "ESP32",
                    .timeout_ms = 30000,
                    .skip_cert_common_name_check = true,
                };
                esp_http_client_handle_t client = esp_http_client_init(&config);
                if (!client) {
                    ESP_LOGE(TAG, "Failed to initialize HTTP client");
                    continue;
                }

                esp_err_t err = esp_http_client_perform(client);

                bool success = false;
                if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    success = (err == ESP_OK && g_downloaded_file_content != NULL && g_downloaded_file_size > 0);
                    if (success) {
                        ESP_LOGI(TAG, "Download successful! Size: %d bytes", g_downloaded_file_size);
                        download_success = true;
                    } else {
                        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
                        if (g_downloaded_file_content) {
                            heap_caps_free(g_downloaded_file_content);
                            g_downloaded_file_content = NULL;
                            g_downloaded_file_size = 0;
                        }
                    }
                    xSemaphoreGive(g_download_mutex);
                }
                esp_http_client_cleanup(client);

                ESP_LOGI(TAG, "Free heap after download: %lu bytes", esp_get_free_heap_size());

                // Add delay between attempts for memory stabilization
                if (!download_success && retry < max_retries - 1) {
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Extra stabilization time
                }
            }
            break; // Exit the while loop
        }
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        elapsed_time_ms += check_interval_ms;
    }

    if (!download_started) {
        ESP_LOGE(TAG, "WiFi did not connect within 60 seconds. Aborting download.");
        // Ensure globals are null/zero so UI shows an error
        if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (g_downloaded_file_content) {
                heap_caps_free(g_downloaded_file_content);
                g_downloaded_file_content = NULL;
            }
            g_downloaded_file_size = 0;
            xSemaphoreGive(g_download_mutex);
        }
    }

    free(url); // Free the URL buffer copied in wifi_download_file

    // Important: Add delay before notifying UI to allow network stack cleanup
    vTaskDelay(pdMS_TO_TICKS(500));

    ui_loading_complete(); // Notify UI that the process is complete (success or failure)
    vTaskDelete(NULL);
}

static void upload_task(void *pvParameters)
{
    char *data_and_file = (char *)pvParameters;

    // Parse the data_and_file string: format is "filename|data"
    char *separator = strchr(data_and_file, '|');
    if (!separator) {
        ESP_LOGE(TAG, "Invalid upload task parameter format");
        free(data_and_file);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    *separator = '\0'; // Split string
    char *filename = data_and_file;
    char *data_content = separator + 1;

    ESP_LOGI(TAG, "Preparando subida a archivo: %s", filename);

    // Synchronize time with SNTP
    if (!sync_time_sntp()) {
        ESP_LOGE(TAG, "No se pudo sincronizar la hora. Abortando subida.");
        free(data_and_file);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    // Get current time and format it
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buffer_fecha[64];
    strftime(buffer_fecha, sizeof(buffer_fecha), "%Y-%m-%d %H:%M:%S", &timeinfo);

    // Create final message with timestamp
    char *buffer_mensaje = malloc(strlen(buffer_fecha) + strlen(data_content) + 10);
    if (!buffer_mensaje) {
        ESP_LOGE(TAG, "Failed to allocate memory for message buffer");
        free(data_and_file);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    snprintf(buffer_mensaje, strlen(buffer_fecha) + strlen(data_content) + 10,
             "%s - %s\n", buffer_fecha, data_content);

    ESP_LOGI(TAG, "Mensaje a enviar: %s", buffer_mensaje);

    // Build URL with key and filename parameters
    char url_with_params[256];
    snprintf(url_with_params, sizeof(url_with_params),
             "%s?key=%s&filename=%s", UPLOAD_URL, API_KEY, filename);

    ESP_LOGI(TAG, "Subiendo a: %s", url_with_params);

    // Configure HTTP POST with browser-like User-Agent to bypass anti-bot security
    // NOTE: No event_handler for upload - we read response manually
    esp_http_client_config_t config = {
        .url = url_with_params,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .skip_cert_common_name_check = true,
        .buffer_size = 2048,
        .buffer_size_tx = 2048
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(buffer_mensaje);
        free(data_and_file);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    // Set headers to bypass anti-bot security
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_header(client, "Accept", "*/*");

    // Set POST data
    esp_http_client_set_post_field(client, buffer_mensaje, strlen(buffer_mensaje));

    // Open connection and send request
    esp_err_t err = esp_http_client_open(client, strlen(buffer_mensaje));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(buffer_mensaje);
        free(data_and_file);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    // Write POST data
    int wlen = esp_http_client_write(client, buffer_mensaje, strlen(buffer_mensaje));
    if (wlen < 0) {
        ESP_LOGE(TAG, "Failed to write POST data");
        esp_http_client_cleanup(client);
        free(buffer_mensaje);
        free(data_and_file);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    // Fetch headers
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP Status = %d, content_length = %d", status_code, content_length);

    bool success = false;

    // Read complete response to debug anti-bot blocks
    if (content_length > 0) {
        char *response_buffer = malloc(content_length + 1);
        if (response_buffer) {
            int total_read = 0;
            while (total_read < content_length) {
                int read_len = esp_http_client_read(client, response_buffer + total_read, content_length - total_read);
                if (read_len <= 0) {
                    ESP_LOGW(TAG, "Fin de lectura o error. Leidos %d de %d bytes", total_read, content_length);
                    break;
                }
                total_read += read_len;
            }
            response_buffer[total_read] = '\0';

            ESP_LOGI(TAG, "=== Respuesta completa del servidor (%d bytes) ===", total_read);
            // Log response in chunks if too long
            if (total_read > 0) {
                const int chunk_size = 200;
                for (int i = 0; i < total_read; i += chunk_size) {
                    int len = (total_read - i > chunk_size) ? chunk_size : (total_read - i);
                    char temp = response_buffer[i + len];
                    response_buffer[i + len] = '\0';
                    ESP_LOGI(TAG, "%s", response_buffer + i);
                    response_buffer[i + len] = temp;
                }
            }
            ESP_LOGI(TAG, "=== Fin de respuesta ===");

            // Check if response contains success message (not HTML)
            if (strstr(response_buffer, "<html") != NULL || strstr(response_buffer, "<!DOCTYPE") != NULL || strstr(response_buffer, "<HTML") != NULL) {
                ESP_LOGW(TAG, "El servidor respondio con HTML (bloqueo anti-bot confirmado). Se esperaba texto plano.");
                success = false;
            } else if (strstr(response_buffer, "guardados") != NULL || strstr(response_buffer, "exitosamente") != NULL || strstr(response_buffer, "success") != NULL) {
                ESP_LOGI(TAG, "Respuesta exitosa detectada en el contenido");
                success = true;
            } else {
                ESP_LOGW(TAG, "Respuesta inesperada. Status 200 pero contenido desconocido.");
                success = (status_code == 200);
            }

            free(response_buffer);
        } else {
            ESP_LOGE(TAG, "No se pudo asignar memoria para leer la respuesta");
            success = false;
        }
    } else if (content_length == 0) {
        ESP_LOGI(TAG, "Respuesta vacia del servidor (puede ser correcto si el script no retorna nada)");
        success = (status_code == 200);
    } else {
        ESP_LOGW(TAG, "content_length negativo: %d", content_length);
        // Try to read anyway
        char temp_buffer[512];
        int read_len = esp_http_client_read(client, temp_buffer, sizeof(temp_buffer) - 1);
        if (read_len > 0) {
            temp_buffer[read_len] = '\0';
            ESP_LOGI(TAG, "Respuesta leida (sin content-length): %s", temp_buffer);
        }
        success = (status_code == 200);
    }

    // Close connection
    esp_http_client_close(client);

    esp_http_client_cleanup(client);
    free(buffer_mensaje);
    free(data_and_file);

    // Notify UI that upload is complete
    ui_upload_complete(success);

    vTaskDelete(NULL);
}

// --- URL ENCODING HELPER ---
static void url_encode(const char *src, char *dst, size_t dst_size) {
    const char *hex = "0123456789ABCDEF";
    size_t dst_idx = 0;

    while (*src && dst_idx < dst_size - 1) {
        if ((*src >= 'A' && *src <= 'Z') ||
            (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9') ||
            *src == '-' || *src == '_' || *src == '.' || *src == '~') {
            // Safe character, copy as-is
            dst[dst_idx++] = *src;
        } else if (*src == ' ') {
            // Space becomes +
            dst[dst_idx++] = '+';
        } else {
            // Encode character as %XX
            if (dst_idx + 3 < dst_size) {
                dst[dst_idx++] = '%';
                dst[dst_idx++] = hex[(*src >> 4) & 0x0F];
                dst[dst_idx++] = hex[*src & 0x0F];
            } else {
                break;  // Not enough space
            }
        }
        src++;
    }
    dst[dst_idx] = '\0';
}



// --- GOOGLE SCRIPT UPLOAD TASK (HTTPS to Google Drive) ---
static void google_script_upload_task(void *pvParameters)
{
    char *url_and_data = (char *)pvParameters;

    // Parse the url_and_data string: format is "url|data"
    char *separator = strchr(url_and_data, '|');
    if (!separator) {
        ESP_LOGE(TAG, "Invalid google script upload parameter format");
        free(url_and_data);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    *separator = '\0'; // Split string
    char *google_script_url = url_and_data;
    char *data_content = separator + 1;

    ESP_LOGI(TAG, "Preparando subida a Google Script...");

    // URL encode the data
    char *encoded_data = malloc(strlen(data_content) * 3 + 1); // Worst case: all chars encoded
    if (!encoded_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for URL encoding");
        free(url_and_data);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    url_encode(data_content, encoded_data, strlen(data_content) * 3 + 1);

    // Build complete URL with data parameter
    char *full_url = malloc(strlen(google_script_url) + strlen(encoded_data) + 20);
    if (!full_url) {
        ESP_LOGE(TAG, "Failed to allocate memory for URL");
        free(encoded_data);
        free(url_and_data);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    snprintf(full_url, strlen(google_script_url) + strlen(encoded_data) + 20,
             "%s?data=%s", google_script_url, encoded_data);

    ESP_LOGI(TAG, "Subiendo a Google Script: %s", google_script_url);

    // Configure HTTP GET for Google Script (HTTPS)
    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,  // Google Scripts can be slow, give it 30 seconds
        .skip_cert_common_name_check = true,  // Workaround for ESP32-P4
        .buffer_size = 2048,
        .buffer_size_tx = 2048
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(full_url);
        free(encoded_data);
        free(url_and_data);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    // Set User-Agent to look like a browser
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

    // Perform HTTP GET request
    esp_err_t err = esp_http_client_perform(client);

    bool success = false;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status = %d", status_code);

        // Read response
        char response_buffer[512];
        int total_read = 0;

        while (1) {
            int read_len = esp_http_client_read(client, response_buffer + total_read, sizeof(response_buffer) - total_read - 1);
            if (read_len <= 0) {
                break;
            }
            total_read += read_len;

            if (total_read >= sizeof(response_buffer) - 1) {
                ESP_LOGW(TAG, "Response buffer full, truncating");
                break;
            }
        }

        response_buffer[total_read] = '\0';

        ESP_LOGI(TAG, "=== Respuesta de Google Script (%d bytes) ===", total_read);
        ESP_LOGI(TAG, "%s", response_buffer);
        ESP_LOGI(TAG, "=== Fin de respuesta ===");

        // Google Script typically returns status 200 or 302 (redirect) on success
        if (status_code == 200 || status_code == 302) {
            ESP_LOGI(TAG, "Subida exitosa a Google Drive!");
            success = true;
        } else {
            ESP_LOGW(TAG, "Codigo de estado inesperado: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Upload failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(full_url);
    free(encoded_data);
    free(url_and_data);

    // Notify UI that upload is complete
    ui_upload_complete(success);

    vTaskDelete(NULL);
}



// --- PUBLIC API FUNCTIONS ---

bool is_wifi_connected(void) {
    return g_wifi_connected;
}

void upload_to_ina(int number) {
    char* data_buffer = malloc(256);
    if (data_buffer) {
        snprintf(data_buffer, 256, "%s|%d", FILE_INA, number);
        xTaskCreate(&upload_task, "upload_task_ina", 8192, data_buffer, 5, NULL);
    }
}

void upload_to_itsaso(int number) {
    char* data_buffer = malloc(256);
    if (data_buffer) {
        snprintf(data_buffer, 256, "%s|%d", FILE_ITSASO, number);
        xTaskCreate(&upload_task, "upload_task_itsaso", 8192, data_buffer, 5, NULL);
    }
}

void upload_text_to_ina(const char *text) {
    // Upload to Google Drive via Google Script
    size_t data_buffer_size = strlen(GOOGLE_SCRIPT_INA) + strlen(text) + 2; // +2 for '|' and '\0'
    char* data_buffer = malloc(data_buffer_size);
    if (data_buffer) {
        snprintf(data_buffer, data_buffer_size, "%s|%s", GOOGLE_SCRIPT_INA, text);
        xTaskCreate(&google_script_upload_task, "google_script_ina", 8192, data_buffer, 5, NULL);
    }
}

void upload_text_to_itsaso(const char *text) {
    // Upload to Google Drive via Google Script
    size_t data_buffer_size = strlen(GOOGLE_SCRIPT_ITSASO) + strlen(text) + 2; // +2 for '|' and '\0'
    char* data_buffer = malloc(data_buffer_size);
    if (data_buffer) {
        snprintf(data_buffer, data_buffer_size, "%s|%s", GOOGLE_SCRIPT_ITSASO, text);
        xTaskCreate(&google_script_upload_task, "google_script_itsaso", 8192, data_buffer, 5, NULL);
    }
}

void wifi_download_file(const char *url) {
    char *url_copy = strdup(url);
    if (url_copy) {
        xTaskCreate(&http_download_task, "http_download_task", 16384, url_copy, 5, NULL);
    }
}

void wifi_download_plan(const char* username) {
    char url_buffer[256];
    const char* SERVER_IP = "80.225.188.163";
    const int SERVER_PORT = 8080;
    snprintf(url_buffer, sizeof(url_buffer), "http://%s:%d/get-plan/%s", SERVER_IP, SERVER_PORT, username);
    wifi_download_file(url_buffer);
}



esp_err_t wifi_client_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Connecting to WiFi network: %s", ssid);

    // Update current credentials
    strlcpy(current_ssid, ssid, sizeof(current_ssid));
    strlcpy(current_password, password, sizeof(current_password));

    // Disconnect from current network
    esp_wifi_disconnect();

    // Configure new network
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, current_ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, current_password, sizeof(wifi_config.sta.password));

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        return err;
    }

    // Connect to new network
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WiFi connection initiated");
    return ESP_OK;
}

void subirDatosPOST_Avanzado(const char* filename, const char* datos_a_enviar) {
    char url_buffer[512];

    // 1. Construir URL usando los macros existentes
    snprintf(url_buffer, sizeof(url_buffer), "%s?key=%s&filename=%s", UPLOAD_POST_URL, API_KEY, filename);
    ESP_LOGI(TAG, "URL: %s", url_buffer);
    ESP_LOGI(TAG, "POST Data: %s", datos_a_enviar);

    // 2. Configurar HTTP POST
    esp_http_client_config_t config = {
        .url = url_buffer,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP client for advanced POST");
        return;
    }

    // 3. Añadir Headers
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
    esp_http_client_set_header(client, "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    esp_http_client_set_header(client, "Accept-Language", "en-US,en;q=0.5");
    esp_http_client_set_header(client, "Referer", "http://entrenadorpersonalia.ct.ws/");
    esp_http_client_set_header(client, "Content-Type", "text/plain");

    // 4. Adjuntar Datos
    esp_http_client_set_post_field(client, datos_a_enviar, strlen(datos_a_enviar));

    // 5. Ejecutar y Leer Respuesta
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d", esp_http_client_get_status_code(client));
        
        char response_buffer[1024] = {0};
        int total_read_len = 0;
        int read_len;

        ESP_LOGI(TAG, "Reading response body...");
        while ((read_len = esp_http_client_read(client, response_buffer + total_read_len, sizeof(response_buffer) - total_read_len - 1)) > 0) {
            total_read_len += read_len;
        }
        
        if (total_read_len > 0) {
            ESP_LOGI(TAG, "Response Body (read %d bytes):", total_read_len);
            printf("%.*s\n", total_read_len, response_buffer);

            if (strstr(response_buffer, "guardados") != NULL) {
                ESP_LOGI(TAG, "Confirmation 'guardados' found in response.");
            } else {
                ESP_LOGW(TAG, "Confirmation 'guardados' NOT found in response.");
            }
        } else {
            ESP_LOGW(TAG, "No response body read.");
        }

    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // 6. Cleanup
    esp_http_client_cleanup(client);
}

// --- ORACLE CLOUD UPLOAD ---

static bool subirDatosOracle(const char* username, const char* datos_a_enviar) {
    char url_buffer[256];
    const char* SERVER_IP = "80.225.188.163";
    const int SERVER_PORT = 8080;

    // 1. Build URL
    snprintf(url_buffer, sizeof(url_buffer), "http://%s:%d/upload/%s", SERVER_IP, SERVER_PORT, username);
    ESP_LOGI(TAG, "Oracle Upload URL: %s", url_buffer);
    ESP_LOGI(TAG, "Oracle POST Data: %s", datos_a_enviar);

    // 2. Configure HTTP POST
    esp_http_client_config_t config = {
        .url = url_buffer,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP client for Oracle upload");
        return false;
    }

    // 3. Add Headers
    esp_http_client_set_header(client, "Content-Type", "text/plain");

    // 4. Attach Data
    esp_http_client_set_post_field(client, datos_a_enviar, strlen(datos_a_enviar));

    // 5. Execute and Read Response
    bool success = false;
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Oracle HTTP POST Status = %d", status_code);

        char response_buffer[512] = {0};
        int total_read_len = 0;
        int read_len;

        while ((read_len = esp_http_client_read(client, response_buffer + total_read_len, sizeof(response_buffer) - total_read_len - 1)) > 0) {
            total_read_len += read_len;
        }

        if (total_read_len > 0) {
            ESP_LOGI(TAG, "Oracle Response Body (read %d bytes): %.*s", total_read_len, total_read_len, response_buffer);
        }

        if (status_code == 200) {
            success = true;
        }

    } else {
        ESP_LOGE(TAG, "Oracle HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // 6. Cleanup
    esp_http_client_cleanup(client);
    return success;
}

static void oracle_upload_task(void *pvParameters) {
    char *task_data = (char *)pvParameters;

    // Parse the task_data string: format is "username|data"
    char *separator = strchr(task_data, '|');
    if (!separator) {
        ESP_LOGE(TAG, "Invalid oracle_upload_task parameter format");
        free(task_data);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    *separator = '\0'; // Split string
    char *username = task_data;
    char *data_content = separator + 1;

    ESP_LOGI(TAG, "Starting Oracle upload for user: %s", username);

    bool success = subirDatosOracle(username, data_content);

    // Notify UI
    ui_upload_complete(success);

    // Cleanup
    free(task_data);
    vTaskDelete(NULL);
}

void upload_to_oracle_ina(const char *text) {
    const char* username = "ina";
    size_t buffer_size = strlen(username) + strlen(text) + 2; // +2 for '|' and '\0'
    char* task_data = malloc(buffer_size);
    if (task_data) {
        snprintf(task_data, buffer_size, "%s|%s", username, text);
        xTaskCreate(&oracle_upload_task, "oracle_upload_ina", 8192, task_data, 5, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for oracle_upload_task (ina)");
        ui_upload_complete(false);
    }
}

void upload_to_oracle_itsaso(const char *text) {
    const char* username = "itsaso";
    size_t buffer_size = strlen(username) + strlen(text) + 2; // +2 for '|' and '\0'
    char* task_data = malloc(buffer_size);
    if (task_data) {
        snprintf(task_data, buffer_size, "%s|%s", username, text);
        xTaskCreate(&oracle_upload_task, "oracle_upload_itsaso", 8192, task_data, 5, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for oracle_upload_task (itsaso)");
        ui_upload_complete(false);
    }
}