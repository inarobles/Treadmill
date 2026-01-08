#ifndef IA_TELEMETRY_H
#define IA_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the IA Telemetry module.
 *        Allocates initial buffers in PSRAM.
 * 
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t ia_telemetry_init(void);

/**
 * @brief Start a new telemetry recording session.
 *        Clears the buffer and starts the 0.5Hz timer.
 * 
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t ia_telemetry_start_session(void);

/**
 * @brief Stop the current telemetry recording session.
 *        Stops the timer.
 */
void ia_telemetry_stop_session(void);

/**
 * @brief Get the current telemetry data as a compressed string.
 *        The format follows the "Flujo informacion.txt" specification:
 *        "v,i,p,c,z; val,val,val,val,val | val,val,val,val,val | ..."
 * 
 * @return const char* Pointer to the internal buffer in PSRAM. 
 *                     Valid until the next session start or deinit.
 */
const char* ia_telemetry_get_current_report(void);

/**
 * @brief Reset the telemetry buffer.
 */
void ia_telemetry_reset(void);

#ifdef __cplusplus
}
#endif

#endif // IA_TELEMETRY_H
