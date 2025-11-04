/**
 * @file cm_protocol.h
 * @brief CM_Protocol_v2.1 - Protocolo de comunicación Consola <-> Sala de Máquinas
 *
 * Definiciones de comandos, constantes y códigos de error del protocolo.
 */

#ifndef CM_PROTOCOL_H
#define CM_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONSTANTES DEL PROTOCOLO
// ============================================================================

/** Byte de inicio de trama (Start of Frame) */
#define CM_SOF              0x3A

/** Byte de escape para byte stuffing */
#define CM_ESC              0x7D

/** XOR aplicado tras ESC en byte stuffing */
#define CM_STUFF_XOR        0x20

/** Tamaño máximo del payload en bytes */
#define CM_MAX_PAYLOAD_LEN  250

/** Tamaño de la cabecera (SOF no se cuenta en la trama lógica, solo LEN+SEQ+CMD) */
#define CM_HEADER_SIZE      3

/** Tamaño del CRC en bytes */
#define CM_CRC_SIZE         2

/** Tamaño mínimo de trama lógica (sin payload): LEN + SEQ + CMD + CRC_H + CRC_L */
#define CM_MIN_FRAME_SIZE   (CM_HEADER_SIZE + CM_CRC_SIZE)

/** Tamaño máximo de trama lógica (con payload máximo) */
#define CM_MAX_FRAME_SIZE   (CM_HEADER_SIZE + CM_MAX_PAYLOAD_LEN + CM_CRC_SIZE)

/** Tamaño máximo de trama física (stuffed) - peor caso: todos los bytes necesitan stuffing */
#define CM_MAX_STUFFED_SIZE (1 + CM_MAX_FRAME_SIZE * 2)  // SOF + worst case stuffing

// ============================================================================
// COMANDOS DEL MAESTRO (Consola -> Sala de Máquinas)
// ============================================================================

/** Establecer velocidad objetivo - Payload: 2 bytes (km/h * 100) */
#define CM_CMD_SET_SPEED            0x11

/** Establecer inclinación objetivo - Payload: 2 bytes (% * 10) */
#define CM_CMD_SET_INCLINE          0x12

/** Control de relé - Payload: 2 bytes (ID_Relé, Estado) */
#define CM_CMD_SET_RELAY            0x13

/** Control de ventilador - Payload: 2 bytes (ID_Ventilador, Estado) */
#define CM_CMD_SET_FAN_STATE        0x14

/** Calibrar inclinación (homing) - Sin payload */
#define CM_CMD_CALIBRATE_INCLINE    0x15

/** Parada de emergencia - Sin payload */
#define CM_CMD_EMERGENCY_STOP       0x1F

/** Solicitar velocidad real del sensor - Sin payload (Heartbeat) */
#define CM_CMD_GET_SENSOR_SPEED     0x21

/** Solicitar estado general - Sin payload (Heartbeat) */
#define CM_CMD_GET_STATUS           0x22

/** Solicitar posición de inclinación - Sin payload */
#define CM_CMD_GET_INCLINE_POSITION 0x23

/** Solicitar estado de ventiladores - Sin payload */
#define CM_CMD_GET_FAN_STATE        0x24

// ============================================================================
// COMANDOS DEL ESCLAVO (Sala de Máquinas -> Consola)
// ============================================================================

/** Confirmación (ACK) - Payload: 1 byte (eco del SEQ) */
#define CM_RSP_ACK                  0x80

/** Rechazo (NAK) - Payload: 2 bytes (eco del SEQ, código de error) */
#define CM_RSP_NAK                  0x81

/** Respuesta: velocidad medida - Payload: 2 bytes (km/h * 100) */
#define CM_RSP_SENSOR_SPEED         0xA1

/** Respuesta: estado general - Payload: 1 byte (bitmap de fallos) */
#define CM_RSP_STATUS               0xA2

/** Respuesta: posición de inclinación - Payload: 2 bytes (% * 10) */
#define CM_RSP_INCLINE_POSITION     0xA3

/** Respuesta: estado de ventiladores - Payload: 2 bytes (head_fan, chest_fan) */
#define CM_RSP_FAN_STATE            0xA4

// ============================================================================
// CÓDIGOS DE ERROR (NAK)
// ============================================================================

/** Error: Comando desconocido */
#define CM_ERR_UNKNOWN_CMD          0xE2

/** Error: Payload inválido (longitud o valor fuera de rango) */
#define CM_ERR_INVALID_PAYLOAD      0xE3

/** Error: Esclavo ocupado */
#define CM_ERR_BUSY                 0xE4

/** Error: Sistema no está listo (ej: no calibrado) */
#define CM_ERR_NOT_READY            0xE5

/** Error: VFD en estado de fallo */
#define CM_ERR_VFD_FAULT            0xE6


// ============================================================================
// ESTRUCTURA DE TRAMA
// ============================================================================

/**
 * @brief Estructura de una trama lógica (antes de stuffing)
 *
 * Formato: [LEN][SEQ][CMD][PAYLOAD...][CRC_H][CRC_L]
 *
 * - LEN: Longitud del PAYLOAD solamente (no incluye SEQ, CMD ni CRC)
 * - SEQ: Número de secuencia (0-255)
 * - CMD: Código de comando
 * - PAYLOAD: Datos del comando (0 a CM_MAX_PAYLOAD_LEN bytes)
 * - CRC: CRC-16/CCITT-FALSE calculado sobre LEN+SEQ+CMD+PAYLOAD
 */
typedef struct {
    uint8_t len;                        ///< Longitud del payload
    uint8_t seq;                        ///< Número de secuencia
    uint8_t cmd;                        ///< Código de comando
    uint8_t payload[CM_MAX_PAYLOAD_LEN]; ///< Datos del comando
    uint16_t crc;                       ///< CRC-16 (big-endian en transmisión)
} cm_frame_t;

// ============================================================================
// FUNCIONES PÚBLICAS - Declaradas en otros headers
// ============================================================================

// CRC-16: Ver cm_crc16.h
// Frame Stuffing/Destuffing: Ver cm_frame.h

#ifdef __cplusplus
}
#endif

#endif // CM_PROTOCOL_H
