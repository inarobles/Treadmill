#ifndef IA_SYNC_H
#define IA_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Data Structures ---

// Defines the maximum number of blocks we expect in a plan (adjust as needed for RAM usage)
#define IA_SYNC_MAX_BLOCKS 20

typedef enum {
    IA_CONDITION_TIME,
    IA_CONDITION_DISTANCE,
    IA_CONDITION_KCAL,
    IA_CONDITION_BPM
} ia_condition_type_t;

typedef struct {
    float target_speed;
    float target_incline;
    float target_bpm;
    
    ia_condition_type_t primary_cond_type;
    float primary_cond_value;
    uint32_t secondary_cond_s;

    char tramo_label[64];
    char bloque_label[64];
} ia_block_t;

typedef struct {
    char plan_id[32];
    int block_count;
    ia_block_t blocks[IA_SYNC_MAX_BLOCKS];
    char raw_json[4096]; // Buffer for debug/display
    // Add metadata if needed (e.g., total duration, description)
} ia_plan_t;

// Callback types for asynchronous events
typedef void (*ia_sync_plan_cb_t)(const ia_plan_t *plan, const char *error_msg);
typedef void (*ia_sync_report_cb_t)(bool success, const char *error_msg);


// --- API Functions ---

/**
 * @brief Initialize the IA Sync Client module.
 *        Starts the background worker task on Core 0.
 * 
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t ia_sync_init(void);

/**
 * @brief Request the next pending training plan for the specified user.
 *        This function is non-blocking. It triggers the background task.
 * 
 * @param user_name "Ina" or "Itsaso"
 * @param callback  Function to call when the operation completes (called from Core 0 context, careful with UI!)
 * @return esp_err_t ESP_OK if request was queued, ESP_FAIL if busy.
 */
esp_err_t ia_sync_get_next_plan(const char *user_name, ia_sync_plan_cb_t callback);

/**
 * @brief Upload a completed training report/telemetry.
 *        This function is non-blocking.
 * 
 * @param user_name "Ina" or "Itsaso"
 * @param plan_id   The ID of the completed plan
 * @param compressed_telemetry  The v,i,p,c,z string compressed data (Must be in PSRAM if large)
 * @param callback  Function to call when upload completes
 * @return esp_err_t ESP_OK if request was queued.
 */
esp_err_t ia_sync_upload_report(const char *user_name, const char *plan_id, const char *compressed_telemetry, ia_sync_report_cb_t callback);


#ifdef __cplusplus
}
#endif

#endif // IA_SYNC_H
