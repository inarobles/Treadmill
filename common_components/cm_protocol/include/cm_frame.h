/**
 * @file cm_frame.h
 * @brief Funciones de byte stuffing/destuffing para CM_Protocol_v2.1
 *
 * Implementa el mecanismo de byte stuffing para evitar que datos con valor
 * SOF (0x3A) sean interpretados como inicio de trama.
 */

#ifndef CM_FRAME_H
#define CM_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include "cm_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Aplica byte stuffing a un buffer de datos
 *
 * Reglas:
 * - 0x3A (SOF) se reemplaza por [0x7D 0x5A]
 * - 0x7D (ESC) se reemplaza por [0x7D 0x5D]
 *
 * @param src Buffer de datos de origen (sin stuffing)
 * @param src_len Longitud del buffer de origen
 * @param dst Buffer de destino (con stuffing)
 * @param dst_max Tamaño máximo del buffer de destino
 * @return Número de bytes escritos en dst, o 0 si dst_max es insuficiente
 *
 * @note El buffer de destino debe tener al menos src_len * 2 bytes
 *       para el peor caso (todos los bytes necesitan stuffing)
 */
size_t cm_stuff_data(const uint8_t *src, size_t src_len,
                     uint8_t *dst, size_t dst_max);

/**
 * @brief Elimina byte stuffing de un buffer de datos
 *
 * Reglas inversas:
 * - [0x7D 0x5A] se restaura a 0x3A
 * - [0x7D 0x5D] se restaura a 0x7D
 *
 * @param src Buffer de datos de origen (con stuffing)
 * @param src_len Longitud del buffer de origen
 * @param dst Buffer de destino (sin stuffing)
 * @param dst_max Tamaño máximo del buffer de destino
 * @return Número de bytes escritos en dst, o 0 si hay error de formato
 *
 * @note Si se encuentra un 0x7D no seguido de 0x5A o 0x5D, se retorna 0 (error)
 */
size_t cm_destuff_data(const uint8_t *src, size_t src_len,
                       uint8_t *dst, size_t dst_max);

/**
 * @brief Construye una trama completa lista para transmitir
 *
 * Esta función:
 * 1. Calcula el CRC sobre [LEN][SEQ][CMD][PAYLOAD]
 * 2. Aplica byte stuffing a toda la trama lógica
 * 3. Añade el SOF al inicio
 *
 * @param frame Estructura de trama con datos a enviar (len, seq, cmd, payload)
 * @param out_buffer Buffer de salida para la trama física (con SOF y stuffing)
 * @param out_max Tamaño máximo del buffer de salida
 * @return Número de bytes de la trama física, o 0 si hay error
 *
 * @note out_buffer debe tener al menos CM_MAX_STUFFED_SIZE bytes
 */
size_t cm_build_frame(const cm_frame_t *frame, uint8_t *out_buffer, size_t out_max);

/**
 * @brief Parsea una trama física (con stuffing) a estructura lógica
 *
 * Esta función:
 * 1. Verifica que comience con SOF (0x3A)
 * 2. Aplica destuffing
 * 3. Verifica el CRC
 * 4. Llena la estructura cm_frame_t
 *
 * @param raw_data Buffer de entrada con trama física (incluyendo SOF)
 * @param raw_len Longitud del buffer de entrada
 * @param frame Estructura de salida para la trama parseada
 * @return true si la trama es válida, false si hay error (CRC o formato)
 *
 * @note Esta función NO consume el SOF del buffer, el caller debe manejarlo
 */
bool cm_parse_frame(const uint8_t *raw_data, size_t raw_len, cm_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif // CM_FRAME_H
