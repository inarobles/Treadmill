/**
 * @file cm_types.h
 * @brief Tipos de datos y estructuras del protocolo CM_Protocol_v2.1
 */

#ifndef CM_TYPES_H
#define CM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "cm_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONVERSIÓN DE VALORES
// ============================================================================

/**
 * @brief Convierte km/h a formato de protocolo (km/h * 100)
 */
static inline uint16_t cm_speed_to_protocol(float speed_kmh) {
    return (uint16_t)(speed_kmh * 100.0f);
}

/**
 * @brief Convierte formato de protocolo a km/h
 */
static inline float cm_speed_from_protocol(uint16_t speed_x100) {
    return (float)speed_x100 / 100.0f;
}

/**
 * @brief Convierte porcentaje de inclinación a formato de protocolo (% * 10)
 */
static inline uint16_t cm_incline_to_protocol(float incline_percent) {
    return (uint16_t)(incline_percent * 10.0f);
}

/**
 * @brief Convierte formato de protocolo a porcentaje de inclinación
 */
static inline float cm_incline_from_protocol(uint16_t incline_x10) {
    return (float)incline_x10 / 10.0f;
}

// ============================================================================
// ESTRUCTURAS DE PAYLOAD ESPECÍFICAS
// ============================================================================

/**
 * @brief Payload para SET_SPEED (2 bytes)
 */
typedef struct {
    uint16_t speed_x100;  ///< Velocidad en km/h * 100 (big-endian)
} cm_payload_set_speed_t;

/**
 * @brief Payload para SET_INCLINE (2 bytes)
 */
typedef struct {
    uint16_t incline_x10;  ///< Inclinación en % * 10 (big-endian)
} cm_payload_set_incline_t;

/**
 * @brief Payload para SET_RELAY (2 bytes)
 */
typedef struct {
    uint8_t relay_id;   ///< ID del relé (0x01 = Bomba Cera, etc.)
    uint8_t state;      ///< Estado (0=OFF, 1=ON)
} cm_payload_set_relay_t;

/**
 * @brief Payload para SET_FAN_STATE (2 bytes)
 */
typedef struct {
    uint8_t fan_id;     ///< ID del ventilador (0x01=Cabeza, 0x02=Pecho)
    uint8_t state;      ///< Estado (0=OFF, 1=50%, 2=100%)
} cm_payload_set_fan_t;

/**
 * @brief Payload para RSP_SENSOR_SPEED (2 bytes)
 */
typedef struct {
    uint16_t speed_x100;  ///< Velocidad medida en km/h * 100 (big-endian)
} cm_payload_rsp_speed_t;

/**
 * @brief Payload para RSP_STATUS (1 byte - bitmap de fallos)
 */
typedef struct {
    uint8_t status;  ///< Bitmap: bit0=VFD_fault, bit1=sensor_fault, etc.
} cm_payload_rsp_status_t;

/**
 * @brief Payload para RSP_INCLINE_POSITION (2 bytes)
 */
typedef struct {
    uint16_t incline_x10;  ///< Posición actual en % * 10 (big-endian)
} cm_payload_rsp_incline_t;

#ifdef __cplusplus
}
#endif

#endif // CM_TYPES_H
