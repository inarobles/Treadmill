#ifndef ACOUSTIC_SERVICE_H
#define ACOUSTIC_SERVICE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Inicializa el servicio de podómetro acústico (Micrófono)
 * 
 * @return esp_err_t ESP_OK en caso de éxito
 */
esp_err_t acoustic_service_init(void);

/**
 * @brief Obtiene el número total de pasos detectados acústicamente
 * 
 * @return uint32_t Conteo de pasos
 */
uint32_t acoustic_service_get_steps(void);

/**
 * @brief Obtiene la cadencia actual (pasos por minuto)
 * 
 * @return float Cadencia en PPM
 */
float acoustic_service_get_cadence(void);

/**
 * @brief Resetea el contador de pasos
 */
void acoustic_service_reset_steps(void);

/**
 * @brief Establece la velocidad actual para ajustar el umbral dinámico
 * 
 * @param speed_kmh Velocidad en km/h
 */
void acoustic_service_set_current_speed(float speed_kmh);

/**
 * @brief Inicia una sesión de calibración acústica (medir ruido ambiental)
 */
void acoustic_service_start_auto_calibration(void);

/**
 * @brief Obtiene el ruido acústico máximo medido en la última calibración
 * 
 * @return float Valor de energía pico (0.0 a 1.0)
 */
float acoustic_service_get_measured_noise(void);

/**
 * @brief Establece el umbral de disparo para una velocidad específica
 * 
 * @param kmh Velocidad entera
 * @param threshold Umbral de energía
 */
void acoustic_service_set_speed_threshold(uint8_t kmh, float threshold);

/**
 * @brief Indica si el servicio está en modo calibración
 */
bool acoustic_service_is_calibrating(void);

/**
 * @brief Activa ventana de silencio de 300ms para filtrar toques en pantalla
 * 
 * Debe llamarse cuando se detecta un evento de touchscreen para evitar
 * que el golpe en la pantalla se cuente como un paso.
 */
void acoustic_service_silence_for_touch(void);

#define MAX_PROFILE_SPEED   25

/**
 * @brief Inicia la captura de 2 segundos de audio y lo vuelca por el puerto serie
 */
void acoustic_service_dump_samples(void);

#endif // ACOUSTIC_SERVICE_H
