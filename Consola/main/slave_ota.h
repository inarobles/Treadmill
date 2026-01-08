#ifndef __SLAVE_OTA_H__
#define __SLAVE_OTA_H__

#include "esp_err.h"

/**
 * @brief Performs an OTA update of the co-processor from a given partition label.
 * 
 * @param partition_label Label of the partition containing the firmware binary.
 * @return esp_err_t ESP_OK on success, failure code otherwise.
 */
esp_err_t slave_ota_perform_from_partition(const char* partition_label);

/**
 * @brief Checks the co-processor version and triggers an update if necessary.
 */
void check_and_update_slave_if_needed(void);

#endif /* __SLAVE_OTA_H__ */
