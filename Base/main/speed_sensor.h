#ifndef SPEED_SENSOR_H
#define SPEED_SENSOR_H

#include "esp_err.h"

/**
 * @brief Inicializa el periférico Pulse Counter (PCNT) en el GPIO 34.
 */
void speed_sensor_init(void);

/**
 * @brief Obtiene el número acumulado de pulsos desde el último reseteo.
 * @return int El número de pulsos.
 */
int speed_sensor_get_pulse_count(void);

#endif // SPEED_SENSOR_H