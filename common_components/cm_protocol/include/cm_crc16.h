/**
 * @file cm_crc16.h
 * @brief CRC-16/CCITT-FALSE para CM_Protocol_v2.1
 *
 * Implementación del algoritmo CRC-16/CCITT-FALSE con lookup table
 * para validación de integridad de tramas.
 *
 * Especificaciones:
 * - Polynomial: 0x1021
 * - Initial Value: 0xFFFF
 * - Input/Output: No reflejado
 * - XOR Out: 0x0000
 */

#ifndef CM_CRC16_H
#define CM_CRC16_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calcula CRC-16/CCITT-FALSE sobre un buffer de datos
 *
 * @param data Puntero al buffer de datos
 * @param len Longitud del buffer en bytes
 * @return CRC-16 calculado (16 bits)
 *
 * @note El CRC se calcula sobre los bytes lógicos (sin byte stuffing)
 *       desde LEN hasta el final de PAYLOAD
 */
uint16_t cm_crc16_calculate(const uint8_t *data, size_t len);

/**
 * @brief Verifica si el CRC de una trama es correcto
 *
 * @param data Puntero a la trama (desde LEN hasta final de PAYLOAD, sin CRC)
 * @param len Longitud de los datos (sin incluir los 2 bytes de CRC)
 * @param received_crc CRC recibido en la trama (big-endian ya convertido a uint16_t)
 * @return true si el CRC es correcto, false en caso contrario
 */
bool cm_crc16_verify(const uint8_t *data, size_t len, uint16_t received_crc);

#ifdef __cplusplus
}
#endif

#endif // CM_CRC16_H
