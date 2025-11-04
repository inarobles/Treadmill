# CM Protocol v2.1 - Componente Común

Biblioteca compartida para el protocolo de comunicación entre Consola (Maestro) y Sala de Máquinas (Esclavo) via RS485.

## Estructura

```
cm_protocol/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── cm_protocol.h      # Definiciones principales (comandos, constantes)
│   ├── cm_types.h         # Estructuras de datos y conversiones
│   ├── cm_crc16.h         # CRC-16/CCITT-FALSE
│   └── cm_frame.h         # Byte stuffing/destuffing
└── src/
    ├── cm_crc16.c         # Implementación CRC con lookup table
    └── cm_frame.c         # Implementación de framing
```

## Características

- **CRC-16/CCITT-FALSE**: Verificación de integridad con lookup table optimizada
- **Byte Stuffing**: Sincronización de tramas (SOF=0x3A, ESC=0x7D)
- **Big-Endian**: Formato de red estándar para datos multi-byte
- **Seguro**: Validación completa de tramas antes de procesarlas

## Formato de Trama

```
Trama física:   [SOF] [stuffed_data...]
Trama lógica:   [LEN] [SEQ] [CMD] [PAYLOAD...] [CRC_H] [CRC_L]
```

- **SOF**: 0x3A (Start of Frame)
- **LEN**: Longitud del PAYLOAD únicamente (0-250)
- **SEQ**: Número de secuencia (0-255) para reintentos
- **CMD**: Código de comando
- **PAYLOAD**: Datos del comando (formato big-endian)
- **CRC**: CRC-16 calculado sobre LEN+SEQ+CMD+PAYLOAD

## Comandos Principales

### Maestro → Esclavo
- `CM_CMD_SET_SPEED` (0x11): Establecer velocidad objetivo
- `CM_CMD_GET_STATUS` (0x22): Solicitar estado (heartbeat)
- `CM_CMD_GET_SENSOR_SPEED` (0x21): Solicitar velocidad real

### Esclavo → Maestro
- `CM_RSP_ACK` (0x80): Confirmación
- `CM_RSP_STATUS` (0xA2): Respuesta de estado
- `CM_RSP_SENSOR_SPEED` (0xA1): Velocidad medida

## Uso Básico

### Enviar una trama

```c
#include "cm_protocol.h"
#include "cm_frame.h"

// Preparar trama
cm_frame_t frame = {
    .len = 2,
    .seq = 10,
    .cmd = CM_CMD_SET_SPEED,
};
frame.payload[0] = 0x07;  // 1950 / 256 = 7 (high byte)
frame.payload[1] = 0x9E;  // 1950 % 256 = 158 (low byte)  -> 19.50 km/h

// Construir trama física
uint8_t tx_buffer[CM_MAX_STUFFED_SIZE];
size_t tx_len = cm_build_frame(&frame, tx_buffer, sizeof(tx_buffer));

// Enviar por UART...
```

### Recibir una trama

```c
// Buffer con trama recibida (incluyendo SOF)
uint8_t rx_buffer[256];
size_t rx_len = /* bytes recibidos */;

// Parsear
cm_frame_t frame;
if (cm_parse_frame(rx_buffer, rx_len, &frame)) {
    // Trama válida
    switch (frame.cmd) {
        case CM_RSP_SENSOR_SPEED:
            uint16_t speed_x100 = (frame.payload[0] << 8) | frame.payload[1];
            float speed_kmh = speed_x100 / 100.0f;
            break;
    }
}
```

## Conversiones de Datos

```c
#include "cm_types.h"

// Velocidad
float speed = 19.5f;  // km/h
uint16_t protocol_speed = cm_speed_to_protocol(speed);  // 1950

// Inclinación
float incline = 12.5f;  // %
uint16_t protocol_incline = cm_incline_to_protocol(incline);  // 125
```

## Test Vectors

CRC-16 test:
- Input: "123456789" (ASCII)
- Expected: 0x29B1

## Notas de Implementación

1. El CRC se calcula ANTES del byte stuffing
2. El byte stuffing se aplica a toda la trama lógica (LEN hasta CRC_L)
3. El SOF (0x3A) NO forma parte de la trama lógica
4. Todos los valores multi-byte usan formato big-endian

## Próximos Pasos

Este componente está listo para ser usado por:
- `cm_master` (en Plantilla/Consola)
- `cm_slave` (en Sala_Maquinas)
