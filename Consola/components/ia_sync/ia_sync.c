#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "ia_sync.h"

#define TAG "IA_SYNC"

// Helper struct for buffering response
struct response_buffer {
    char *ptr;
    size_t len;
    size_t max_len;
};

// Configuration constants from Kconfig
#define SCRIPT_URL CONFIG_IA_SYNC_SCRIPT_URL
#define DEVICE_TYPE CONFIG_IA_SYNC_DEVICE_STRING

// Request types
typedef enum {
    REQ_GET_PLAN,
    REQ_UPLOAD_REPORT
} ia_req_type_t;

// Queue item structure
typedef struct {
    ia_req_type_t type;
    char user[32];
    char plan_id[32]; // For report upload
    char *telemetry_data; // Pointer to PSRAM data
    ia_sync_plan_cb_t plan_cb;
    ia_sync_report_cb_t report_cb;
} ia_queue_item_t;

static QueueHandle_t s_req_queue = NULL;
static bool s_is_initialized = false;

// Function prototypes
static void ia_sync_task(void *arg);
static esp_err_t perform_get_plan(const char *user, ia_plan_t *out_plan);
static esp_err_t perform_upload_report(const char *user, const char *plan_id, const char *telemetry);

esp_err_t ia_sync_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // Use 10 items for better buffering under load
    s_req_queue = xQueueCreate(10, sizeof(ia_queue_item_t));
    if (s_req_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_FAIL;
    }

    // Pin to Core 1 (Standard for network tasks on dual core ESPs)
    BaseType_t ret = xTaskCreatePinnedToCore(
        ia_sync_task, 
        "ia_sync_task", 
        20480, 
        NULL, 
        5, 
        NULL, 
        1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(s_req_queue);
        s_req_queue = NULL;
        return ESP_FAIL;
    }

    s_is_initialized = true;
    esp_log_level_set("cache", ESP_LOG_NONE); // Disable cache sync errors again just in case
    ESP_LOGI(TAG, "IA Sync Component initialized on Core 1");
    return ESP_OK;
}

esp_err_t ia_sync_get_next_plan(const char *user_name, ia_sync_plan_cb_t callback)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (!user_name || !callback) return ESP_ERR_INVALID_ARG;

    ia_queue_item_t item;
    item.type = REQ_GET_PLAN;
    strncpy(item.user, user_name, sizeof(item.user) - 1);
    item.plan_cb = callback;
    item.telemetry_data = NULL;

    if (xQueueSend(s_req_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Queue full");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ia_sync_upload_report(const char *user_name, const char *plan_id, const char *compressed_telemetry, ia_sync_report_cb_t callback)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (!user_name || !plan_id || !compressed_telemetry) return ESP_ERR_INVALID_ARG;

    char *telemetry_copy = NULL;
    size_t len = strlen(compressed_telemetry);
    telemetry_copy = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM); 
    if (!telemetry_copy) {
         telemetry_copy = malloc(len + 1);
    }
    
    if (!telemetry_copy) {
        ESP_LOGE(TAG, "Failed to allocate memory for telemetry copy");
        return ESP_ERR_NO_MEM;
    }
    strcpy(telemetry_copy, compressed_telemetry);

    ia_queue_item_t item;
    item.type = REQ_UPLOAD_REPORT;
    strncpy(item.user, user_name, sizeof(item.user) - 1);
    strncpy(item.plan_id, plan_id, sizeof(item.plan_id) - 1);
    item.telemetry_data = telemetry_copy; 
    item.report_cb = callback;

    if (xQueueSend(s_req_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Queue full");
        free(telemetry_copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void ia_sync_task(void *arg)
{
    ia_queue_item_t item;
    while (1) {
        if (xQueueReceive(s_req_queue, &item, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Processing request type: %d for user: %s", item.type, item.user);

            if (item.type == REQ_GET_PLAN) {
                // Use 128-byte aligned internal RAM for the plan structure
                ia_plan_t *p_plan = heap_caps_aligned_alloc(128, sizeof(ia_plan_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (p_plan) {
                    memset(p_plan, 0, sizeof(ia_plan_t));
                    esp_err_t err = perform_get_plan(item.user, p_plan);
                    if (err == ESP_OK) {
                        if (item.plan_cb) item.plan_cb(p_plan, NULL);
                    } else {
                        if (item.plan_cb) item.plan_cb(NULL, "Network Error or No Plan");
                    }
                    free(p_plan);
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for plan parsing");
                    if (item.plan_cb) item.plan_cb(NULL, "Memory Error");
                }
            } else if (item.type == REQ_UPLOAD_REPORT) {
                esp_err_t err = perform_upload_report(item.user, item.plan_id, item.telemetry_data);
                if (err == ESP_OK) {
                    if (item.report_cb) item.report_cb(true, NULL);
                } else {
                    if (item.report_cb) item.report_cb(false, "Upload Failed");
                }
                
                if (item.telemetry_data) {
                    free(item.telemetry_data);
                }
            }
        }
    }
}

static esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            if (evt->user_data) {
                ((struct response_buffer *)evt->user_data)->len = 0;
            }
            break;
        case HTTP_EVENT_ON_DATA:
            if (evt->user_data) {
                struct response_buffer *pbuf = (struct response_buffer *)evt->user_data;
                if (pbuf->len + evt->data_len < pbuf->max_len) {
                    memcpy(pbuf->ptr + pbuf->len, evt->data, evt->data_len);
                    pbuf->len += evt->data_len;
                    if (pbuf->len < pbuf->max_len) {
                        pbuf->ptr[pbuf->len] = 0; 
                    }
                } else {
                    ESP_LOGW(TAG, "Response buffer overflow!");
                }
            }
            break;
        default: break;
    }
    return ESP_OK;
}

static esp_err_t perform_get_plan(const char *user, ia_plan_t *out_plan)
{
    char url[512];
    snprintf(url, sizeof(url), "%s?user=%s&dispositivo=%s", SCRIPT_URL, user, DEVICE_TYPE);
    
    // Use heap-allocated config to avoid PSRAM stack issues on P4
    esp_http_client_config_t *config = heap_caps_aligned_alloc(128, sizeof(esp_http_client_config_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!config) return ESP_ERR_NO_MEM;
    memset(config, 0, sizeof(esp_http_client_config_t));
    
    config->url = url;
    config->event_handler = _http_event_handle;
    config->crt_bundle_attach = esp_crt_bundle_attach;
    config->timeout_ms = 10000;
    config->disable_auto_redirect = false;
    config->max_redirection_count = 5;
    config->buffer_size = 2048;
    config->buffer_size_tx = 2048;

    esp_http_client_handle_t client = esp_http_client_init(config);
    heap_caps_free(config);
    if (!client) return ESP_FAIL;
    
    // Allocate respondent buffer shell on heap too
    struct response_buffer *resp_buffer = heap_caps_aligned_alloc(128, sizeof(struct response_buffer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!resp_buffer) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    size_t max_size = 8192;
    // Use 128-byte alignment for P4 L2 cache and internal RAM
    resp_buffer->ptr = heap_caps_aligned_alloc(128, max_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!resp_buffer->ptr) {
         heap_caps_free(resp_buffer);
         esp_http_client_cleanup(client);
         return ESP_ERR_NO_MEM;
    }
    memset(resp_buffer->ptr, 0, max_size);
    resp_buffer->len = 0;
    resp_buffer->max_len = max_size;

    esp_http_client_set_user_data(client, resp_buffer);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if ((status_code == 200) && (resp_buffer->len > 0)) {
            strncpy(out_plan->raw_json, resp_buffer->ptr, sizeof(out_plan->raw_json)-1);
            cJSON *root = cJSON_Parse(resp_buffer->ptr);
            if (root) {
                cJSON *pid = cJSON_GetObjectItem(root, "plan_id");
                cJSON *datos = cJSON_GetObjectItem(root, "datos");
                if (pid && datos) {
                    strncpy(out_plan->plan_id, pid->valuestring, sizeof(out_plan->plan_id)-1);
                    cJSON *tramos = cJSON_GetObjectItem(datos, "tramos");
                    int total_blocks = 0;
                    if (tramos && cJSON_IsArray(tramos)) {
                        int tramo_count = cJSON_GetArraySize(tramos);
                        for (int i = 0; i < tramo_count && total_blocks < IA_SYNC_MAX_BLOCKS; i++) {
                            cJSON *tramo = cJSON_GetArrayItem(tramos, i);
                            cJSON *t_ui = cJSON_GetObjectItem(tramo, "ui_contexto");
                            const char *t_name = t_ui ? cJSON_GetObjectItem(t_ui, "nombre_tramo")->valuestring : "Entreno";
                            
                            cJSON *bloques = cJSON_GetObjectItem(tramo, "bloques");
                            if (bloques && cJSON_IsArray(bloques)) {
                                int bloque_count = cJSON_GetArraySize(bloques);
                                for (int j = 0; j < bloque_count && total_blocks < IA_SYNC_MAX_BLOCKS; j++) {
                                    cJSON *bloque = cJSON_GetArrayItem(bloques, j);
                                    cJSON *b_ui = cJSON_GetObjectItem(bloque, "ui_contexto");
                                    const char *b_name = b_ui ? cJSON_GetObjectItem(b_ui, "desc_bloque")->valuestring : "Tramo";
                                    
                                    cJSON *v_f = cJSON_GetObjectItem(bloque, "v_final");
                                    cJSON *i_f = cJSON_GetObjectItem(bloque, "inc_final");
                                    cJSON *b_p = cJSON_GetObjectItem(bloque, "bpm_objetivo");
                                    
                                    out_plan->blocks[total_blocks].target_speed = v_f ? (float)v_f->valuedouble : 0.0f;
                                    out_plan->blocks[total_blocks].target_incline = i_f ? (float)i_f->valuedouble : 0.0f;
                                    out_plan->blocks[total_blocks].target_bpm = b_p ? (float)b_p->valuedouble : 0.0f;

                                    // Fin primario
                                    cJSON *f_p = cJSON_GetObjectItem(bloque, "fin_primario");
                                    if (f_p) {
                                        cJSON *f_type = cJSON_GetObjectItem(f_p, "metrica");
                                        cJSON *f_val = cJSON_GetObjectItem(f_p, "valor");
                                        if (f_type && f_val) {
                                            const char *metrica = f_type->valuestring;
                                            if (strcmp(metrica, "TIEMPO") == 0) out_plan->blocks[total_blocks].primary_cond_type = IA_CONDITION_TIME;
                                            else if (strcmp(metrica, "DISTANCIA") == 0) out_plan->blocks[total_blocks].primary_cond_type = IA_CONDITION_DISTANCE;
                                            else if (strcmp(metrica, "KCAL") == 0) out_plan->blocks[total_blocks].primary_cond_type = IA_CONDITION_KCAL;
                                            else if (strcmp(metrica, "BPM") == 0) out_plan->blocks[total_blocks].primary_cond_type = IA_CONDITION_BPM;
                                            
                                            out_plan->blocks[total_blocks].primary_cond_value = (float)f_val->valuedouble;
                                        }
                                    }

                                    // Fin secundario (Seguridad)
                                    cJSON *f_s = cJSON_GetObjectItem(bloque, "fin_secundario");
                                    if (f_s) {
                                        cJSON *f_val = cJSON_GetObjectItem(f_s, "valor");
                                        out_plan->blocks[total_blocks].secondary_cond_s = f_val ? (uint32_t)f_val->valueint : 0;
                                    }

                                    strncpy(out_plan->blocks[total_blocks].tramo_label, t_name, 63);
                                    strncpy(out_plan->blocks[total_blocks].bloque_label, b_name, 63);
                                    total_blocks++;
                                }
                            }
                        }
                        out_plan->block_count = total_blocks;
                        err = (total_blocks > 0) ? ESP_OK : ESP_FAIL;
                    }
                }
                cJSON_Delete(root);
            } else { err = ESP_FAIL; }
        } else { err = ESP_FAIL; }
    }
    if (resp_buffer->ptr) heap_caps_free(resp_buffer->ptr);
    heap_caps_free(resp_buffer);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t perform_upload_report(const char *user, const char *plan_id, const char *telemetry)
{
    char url[512];
    snprintf(url, sizeof(url), "%s?user=%s&dispositivo=%s&action=upload", SCRIPT_URL, user, DEVICE_TYPE);

    // Use heap-allocated config to avoid PSRAM stack issues on P4
    esp_http_client_config_t *config = heap_caps_aligned_alloc(128, sizeof(esp_http_client_config_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!config) return ESP_ERR_NO_MEM;
    memset(config, 0, sizeof(esp_http_client_config_t));

    config->url = url;
    config->event_handler = _http_event_handle;
    config->crt_bundle_attach = esp_crt_bundle_attach;
    config->timeout_ms = 30000;
    config->method = HTTP_METHOD_POST;
    config->buffer_size = 2048;
    config->buffer_size_tx = 2048;
    config->disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(config);
    heap_caps_free(config);
    if (!client) return ESP_FAIL;
    
    // Allocate respondent buffer shell on heap
    struct response_buffer *resp_buffer = heap_caps_aligned_alloc(128, sizeof(struct response_buffer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!resp_buffer) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    // Use 128-byte alignment for P4 L2 cache and internal memory
    resp_buffer->ptr = heap_caps_aligned_alloc(128, 8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (resp_buffer->ptr) {
        resp_buffer->len = 0; resp_buffer->max_len = 8192;
        esp_http_client_set_user_data(client, resp_buffer);
    } else {
        resp_buffer->len = 0; resp_buffer->max_len = 0;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "user", user);
    cJSON_AddStringToObject(root, "dispositivo", DEVICE_TYPE);
    cJSON_AddStringToObject(root, "plan_id", plan_id);
    cJSON_AddNumberToObject(root, "parte", 1); // Indice del fragmento
    cJSON_AddStringToObject(root, "telemetria_hd", telemetry);
    // Para compatibilidad con versiones anteriores del script si fuera necesario
    cJSON_AddStringToObject(root, "telemetria", telemetry);
    
    char *json_raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_raw) {
        if (resp_buffer->ptr) heap_caps_free(resp_buffer->ptr);
        heap_caps_free(resp_buffer);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    // Force JSON string to Internal RAM with 128-byte alignment to avoid esp_cache_msync issues on P4
    size_t json_len = strlen(json_raw);
    char *json_str = heap_caps_aligned_alloc(128, json_len + 128, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!json_str) {
        free(json_raw);
        if (resp_buffer->ptr) heap_caps_free(resp_buffer->ptr);
        heap_caps_free(resp_buffer);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    memset(json_str, 0, json_len + 128);
    strcpy(json_str, json_raw);
    free(json_raw);
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, json_len);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);

    if (err == ESP_OK && (status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308)) {
        if (resp_buffer->ptr) resp_buffer->len = 0;
        err = esp_http_client_set_redirection(client);
        if (err == ESP_OK) {
             esp_http_client_set_method(client, HTTP_METHOD_GET);
             esp_http_client_set_post_field(client, NULL, 0);
             err = esp_http_client_perform(client);
             status_code = esp_http_client_get_status_code(client);
        }
        err = ESP_OK; 
    }

    if (err == ESP_OK) {
        if (!(status_code == 200 || status_code == 201 || status_code == 405)) err = ESP_FAIL;
    }

    if (resp_buffer->ptr) heap_caps_free(resp_buffer->ptr);
    heap_caps_free(resp_buffer);
    if (json_str) heap_caps_free(json_str);
    esp_http_client_cleanup(client);
    return err;
}
