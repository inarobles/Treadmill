#ifndef IMU_SERVICE_H
#define IMU_SERVICE_H

#include "esp_err.h"

/**
 * @brief Initialize the IMU service (BMI270 + BMM150)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t imu_service_init(void);

/**
 * @brief Get the current cumulative step count
 * 
 * @return uint32_t Total steps since reset
 */
uint32_t imu_service_get_steps(void);

/**
 * @brief Get the current cadence in steps per minute (SPM)
 * 
 * @return float Cadence (SPM)
 */
float imu_service_get_cadence(void);

/**
 * @brief Set the pedometer sensitivity
 * 
 * @param sensitivity_percent 0-100% (0: least sensitive, 100: most sensitive)
 */
void imu_service_set_sensitivity(uint8_t sensitivity_percent);

#endif // IMU_SERVICE_H

