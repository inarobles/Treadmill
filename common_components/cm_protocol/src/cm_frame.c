/**
 * @file cm_frame.c
 * @brief Implementación de byte stuffing/destuffing y construcción de tramas
 */

#include "cm_frame.h"
#include "cm_crc16.h"
#include <string.h>

// ============================================================================
// BYTE STUFFING
// ============================================================================

size_t cm_stuff_data(const uint8_t *src, size_t src_len,
                     uint8_t *dst, size_t dst_max) {
    if (!src || !dst || dst_max == 0) {
        return 0;
    }

    size_t dst_idx = 0;

    for (size_t i = 0; i < src_len; i++) {
        uint8_t byte = src[i];

        if (byte == CM_SOF || byte == CM_ESC) {
            // Necesitamos 2 bytes: ESC + (byte XOR 0x20)
            if (dst_idx + 2 > dst_max) {
                return 0;  // Buffer insuficiente
            }
            dst[dst_idx++] = CM_ESC;
            dst[dst_idx++] = byte ^ CM_STUFF_XOR;
        } else {
            // Byte normal, copiar directamente
            if (dst_idx + 1 > dst_max) {
                return 0;  // Buffer insuficiente
            }
            dst[dst_idx++] = byte;
        }
    }

    return dst_idx;
}

// ============================================================================
// BYTE DESTUFFING
// ============================================================================

size_t cm_destuff_data(const uint8_t *src, size_t src_len,
                       uint8_t *dst, size_t dst_max) {
    if (!src || !dst || dst_max == 0) {
        return 0;
    }

    size_t dst_idx = 0;
    size_t src_idx = 0;

    while (src_idx < src_len) {
        uint8_t byte = src[src_idx++];

        if (byte == CM_ESC) {
            // Siguiente byte debe ser 0x5A (para 0x3A) o 0x5D (para 0x7D)
            if (src_idx >= src_len) {
                return 0;  // Error: ESC sin siguiente byte
            }

            uint8_t next_byte = src[src_idx++];
            byte = next_byte ^ CM_STUFF_XOR;

            // Verificar que sea válido (debe resultar en SOF o ESC)
            if (byte != CM_SOF && byte != CM_ESC) {
                return 0;  // Error: secuencia de escape inválida
            }
        }

        // Copiar byte destuffeado
        if (dst_idx >= dst_max) {
            return 0;  // Buffer insuficiente
        }
        dst[dst_idx++] = byte;
    }

    return dst_idx;
}

// ============================================================================
// CONSTRUCCIÓN DE TRAMA
// ============================================================================

size_t cm_build_frame(const cm_frame_t *frame, uint8_t *out_buffer, size_t out_max) {
    if (!frame || !out_buffer || out_max < CM_MAX_STUFFED_SIZE) {
        return 0;
    }

    // Validar longitud del payload
    if (frame->len > CM_MAX_PAYLOAD_LEN) {
        return 0;
    }

    // 1. Construir trama lógica: [LEN][SEQ][CMD][PAYLOAD]
    uint8_t logical_frame[CM_MAX_FRAME_SIZE];
    size_t logical_idx = 0;

    logical_frame[logical_idx++] = frame->len;
    logical_frame[logical_idx++] = frame->seq;
    logical_frame[logical_idx++] = frame->cmd;

    // Copiar payload
    if (frame->len > 0) {
        memcpy(&logical_frame[logical_idx], frame->payload, frame->len);
        logical_idx += frame->len;
    }

    // 2. Calcular CRC sobre [LEN][SEQ][CMD][PAYLOAD]
    uint16_t crc = cm_crc16_calculate(logical_frame, logical_idx);

    // 3. Añadir CRC (big-endian)
    logical_frame[logical_idx++] = (crc >> 8) & 0xFF;  // CRC_H
    logical_frame[logical_idx++] = crc & 0xFF;         // CRC_L

    // 4. Aplicar byte stuffing (desde LEN hasta CRC_L)
    uint8_t stuffed_buffer[CM_MAX_STUFFED_SIZE - 1];  // -1 porque SOF va aparte
    size_t stuffed_len = cm_stuff_data(logical_frame, logical_idx,
                                       stuffed_buffer, sizeof(stuffed_buffer));

    if (stuffed_len == 0) {
        return 0;  // Error en stuffing
    }

    // 5. Construir trama física: [SOF][stuffed_data]
    if (1 + stuffed_len > out_max) {
        return 0;  // Buffer de salida insuficiente
    }

    out_buffer[0] = CM_SOF;
    memcpy(&out_buffer[1], stuffed_buffer, stuffed_len);

    return 1 + stuffed_len;
}

// ============================================================================
// PARSEO DE TRAMA
// ============================================================================

bool cm_parse_frame(const uint8_t *raw_data, size_t raw_len, cm_frame_t *frame) {
    if (!raw_data || !frame || raw_len < 2) {  // Mínimo: SOF + algo
        return false;
    }

    // 1. Verificar SOF
    if (raw_data[0] != CM_SOF) {
        return false;
    }

    // 2. Aplicar destuffing (desde después del SOF)
    uint8_t logical_frame[CM_MAX_FRAME_SIZE];
    size_t logical_len = cm_destuff_data(&raw_data[1], raw_len - 1,
                                         logical_frame, sizeof(logical_frame));

    if (logical_len == 0) {
        return false;  // Error en destuffing
    }

    // 3. Verificar longitud mínima: LEN + SEQ + CMD + CRC_H + CRC_L = 5 bytes
    if (logical_len < CM_MIN_FRAME_SIZE) {
        return false;
    }

    // 4. Extraer campos
    uint8_t len = logical_frame[0];
    uint8_t seq = logical_frame[1];
    uint8_t cmd = logical_frame[2];

    // 5. Verificar que la longitud declarada coincide
    size_t expected_len = CM_HEADER_SIZE + len + CM_CRC_SIZE;  // LEN+SEQ+CMD + payload + CRC
    if (logical_len != expected_len) {
        return false;
    }

    // 6. Extraer CRC (big-endian)
    size_t crc_offset = CM_HEADER_SIZE + len;
    uint16_t received_crc = ((uint16_t)logical_frame[crc_offset] << 8) |
                            logical_frame[crc_offset + 1];

    // 7. Verificar CRC (calculado sobre LEN hasta fin de PAYLOAD)
    if (!cm_crc16_verify(logical_frame, CM_HEADER_SIZE + len, received_crc)) {
        return false;  // CRC inválido
    }

    // 8. Llenar estructura de salida
    frame->len = len;
    frame->seq = seq;
    frame->cmd = cmd;
    frame->crc = received_crc;

    if (len > 0) {
        memcpy(frame->payload, &logical_frame[CM_HEADER_SIZE], len);
    }

    return true;
}
