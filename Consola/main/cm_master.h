/**
 * @file cm_master.h
 * @brief CM Protocol Master - Consola (Maestro)
 *
 * Módulo maestro que envía comandos a Sala de Máquinas via RS485
 * usando el protocolo CM_Protocol_v2.1
 */

#ifndef CM_MASTER_H
#define CM_MASTER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONFIGURACIÓN
// ============================================================================

/** Puerto UART para RS485 */
#define CM_MASTER_UART_PORT     UART_NUM_1

/** Baudrate */
#define CM_MASTER_BAUD_RATE     115200

/** Pin TX (según hardware) */
#define CM_MASTER_TX_PIN        4

/** Pin RX (según hardware) */
#define CM_MASTER_RX_PIN        5

/** Intervalo de heartbeat (GET_STATUS) en ms */
#define CM_MASTER_HEARTBEAT_MS  300

// ============================================================================
// FUNCIONES PÚBLICAS
// ============================================================================

/**
 * @brief Inicializa el módulo maestro
 *
 * Configura UART y estructuras internas.
 * NO crea tareas todavía.
 *
 * @return ESP_OK si éxito, error en caso contrario
 */
esp_err_t cm_master_init(void);

/**
 * @brief Inicia la tarea del maestro
 *
 * Crea la tarea que enviará heartbeats y comandos.
 *
 * @return ESP_OK si éxito, error en caso contrario
 */
esp_err_t cm_master_start(void);

/**
 * @brief Envía comando SET_SPEED al esclavo
 *
 * @param speed_kmh Velocidad objetivo en km/h
 * @return ESP_OK si éxito, error en caso contrario
 */
esp_err_t cm_master_set_speed(float speed_kmh);

/**
 * @brief Envía comando SET_INCLINE al esclavo
 *
 * @param incline_pct Inclinación objetivo en % (0-15%)
 * @return ESP_OK si éxito, error en caso contrario
 */
esp_err_t cm_master_set_incline(float incline_pct);

/**
 * @brief Envía comando CALIBRATE_INCLINE al esclavo (homing a 0%)
 *
 * @return ESP_OK si éxito, error en caso contrario
 */
esp_err_t cm_master_calibrate_incline(void);

/**
 * @brief Obtiene el estado de comunicación
 *
 * @return true si hay comunicación con el esclavo, false en caso contrario
 */
bool cm_master_is_connected(void);

/**
 * @brief Obtiene la velocidad real del sensor (último valor recibido)
 *
 * @return Velocidad en km/h, o 0.0 si no hay datos
 */
float cm_master_get_real_speed(void);

/**
 * @brief Obtiene la inclinación actual del motor lineal (último valor recibido de GET_INCLINE_POSITION)
 *
 * @return Inclinación en %, o 0.0 si no hay datos
 */
float cm_master_get_current_incline(void);

/**
 * @brief Envía comando SET_FAN_STATE al esclavo
 *
 * @param fan_id ID del ventilador (0x01=HEAD, 0x02=CHEST)
 * @param state Estado del ventilador (0=OFF, 1=50%, 2=100%)
 * @return ESP_OK si éxito, error en caso contrario
 */
esp_err_t cm_master_set_fan(uint8_t fan_id, uint8_t state);

/**
 * @brief Obtiene el estado del ventilador de cabeza (último valor recibido)
 *
 * @return Estado (0/1/2), o 0 si no hay datos
 */
uint8_t cm_master_get_head_fan_state(void);

/**
 * @brief Obtiene el estado del ventilador de pecho (último valor recibido)
 *
 * @return Estado (0/1/2), o 0 si no hay datos
 */
uint8_t cm_master_get_chest_fan_state(void);

/**
 * @brief Envía comando SET_RELAY al esclavo
 *
 * @param relay_id ID del relé (0x01=WAX_PUMP)
 * @param state Estado del relé (0=OFF, 1=ON)
 * @return ESP_OK si éxito, error en caso contrario
 */
esp_err_t cm_master_set_relay(uint8_t relay_id, uint8_t state);

#ifdef __cplusplus
}
#endif

#endif // CM_MASTER_H
