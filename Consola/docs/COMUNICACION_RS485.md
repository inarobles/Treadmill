# Comunicación RS485 - Maestro CM Protocol v2.1

Documentación completa del módulo maestro de comunicación RS485 con el sistema Base.

## Índice

- [Descripción General](#descripción-general)
- [Protocolo CM_Protocol v2.1](#protocolo-cm_protocol-v21)
- [Arquitectura del Maestro](#arquitectura-del-maestro)
- [Sistema de Comandos](#sistema-de-comandos)
- [Sistema de Reintentos y Timeouts](#sistema-de-reintentos-y-timeouts)
- [Estados y Detección de Conexión](#estados-y-detección-de-conexión)
- [API Pública](#api-pública)
- [Ejemplos de Uso](#ejemplos-de-uso)

## Descripción General

El módulo `cm_master.c/h` implementa el lado MAESTRO del protocolo CM_Protocol v2.1, permitiendo a la Consola controlar el módulo Base (esclavo) mediante comunicación RS485.

**Características**:
- Sistema asíncrono con cola de comandos
- Timeouts y reintentos automáticos
- Heartbeat continuo (300ms)
- Detección de desconexión
- Thread-safe (protegido por mutex)

**Comunicación**:
- **Interface**: RS485 vía UART1
- **Baudrate**: 115200
- **Pines**: TX=GPIO4, RX=GPIO5
- **Protocolo**: CM_Protocol v2.1 (ver `common_components/cm_protocol`)

## Protocolo CM_Protocol v2.1

### Formato de Trama

```
Trama Física:   [SOF] [stuffed_data...]
Trama Lógica:   [LEN] [SEQ] [CMD] [PAYLOAD...] [CRC_H] [CRC_L]
```

**Elementos**:
- **SOF**: 0x3A (Start of Frame)
- **LEN**: Longitud del PAYLOAD (0-250)
- **SEQ**: Número de secuencia (0-255)
- **CMD**: Código de comando
- **PAYLOAD**: Datos (formato big-endian)
- **CRC**: CRC-16/CCITT-FALSE

**Byte Stuffing**:
- ESC byte: 0x7D
- XOR mask: 0x20
- Se aplica a SOF y ESC en la trama lógica

### Comandos Disponibles

| Comando | Código | Payload | Respuesta | Timeout |
|---------|--------|---------|-----------|---------|
| `GET_STATUS` | 0x22 | - | RSP_STATUS | 100ms |
| `GET_SENSOR_SPEED` | 0x21 | - | RSP_SENSOR_SPEED | 100ms |
| `GET_INCLINE_POSITION` | 0x23 | - | RSP_INCLINE_POSITION | 100ms |
| `GET_FAN_STATE` | 0x24 | - | RSP_FAN_STATE | 100ms |
| `SET_SPEED` | 0x11 | 2 bytes (km/h×100) | ACK/NAK | 100ms |
| `SET_INCLINE` | 0x12 | 2 bytes (%×10) | ACK/NAK | 100ms |
| `CALIBRATE_INCLINE` | 0x15 | - | ACK/NAK | 100ms |
| `SET_FAN_STATE` | 0x14 | 2 bytes (ID, State) | ACK/NAK | 100ms |
| `SET_RELAY` | 0x13 | 2 bytes (ID, State) | ACK/NAK | 100ms |
| `EMERGENCY_STOP` | 0x1F | - | ACK/NAK | 100ms |

### Respuestas del Esclavo

| Respuesta | Código | Payload | Descripción |
|-----------|--------|---------|-------------|
| `ACK` | 0x80 | 1 byte (SEQ echo) | Confirmación |
| `NAK` | 0x81 | 2 bytes (SEQ, error) | Rechazo |
| `RSP_STATUS` | 0xA2 | 1 byte (bitmap) | Estado general |
| `RSP_SENSOR_SPEED` | 0xA1 | 2 bytes (km/h×100) | Velocidad medida |
| `RSP_INCLINE_POSITION` | 0xA3 | 2 bytes (%×10) | Inclinación actual |
| `RSP_FAN_STATE` | 0xA4 | 2 bytes (head, chest) | Estado ventiladores |

**Códigos de Error NAK**:
- `0xE2`: Comando desconocido
- `0xE3`: Payload inválido
- `0xE4`: Sistema ocupado
- `0xE5`: No listo (no calibrado)
- `0xE6`: VFD en fallo

## Arquitectura del Maestro

### Componentes Principales

```
┌──────────────────────────────────────────────────────┐
│               cm_master.c                            │
│                                                      │
│  ┌────────────────┐       ┌────────────────┐       │
│  │  Master Task   │       │   RX Task      │       │
│  │  (Heartbeat)   │       │  (Recepción)   │       │
│  └────────┬───────┘       └────────┬───────┘       │
│           │                        │               │
│           v                        v               │
│  ┌────────────────────────────────────────┐       │
│  │     Cola de Comandos Pendientes       │       │
│  │   [Slot 0] [Slot 1] [Slot 2] [Slot 3] │       │
│  └────────────────────────────────────────┘       │
│           │                        │               │
│           v                        v               │
│  ┌────────────────┐       ┌────────────────┐       │
│  │ Watchdog Task  │       │ Parser         │       │
│  │ (Timeouts)     │       │ (Destuffing)   │       │
│  └────────────────┘       └────────────────┘       │
│           │                        │               │
└───────────┼────────────────────────┼───────────────┘
            │                        │
            v                        v
       UART1 TX                 UART1 RX
         (GPIO 4)                (GPIO 5)
            │                        │
            └────────┬───────────────┘
                     │
                   MAX485
                     │
                 RS485 Bus
                     │
              Módulo Base (Esclavo)
```

### Tareas FreeRTOS

1. **master_task** (Prioridad 6, Stack 4KB)
   - Envía heartbeat cada 300ms
   - Envía SET_SPEED / SET_INCLINE cuando cambian objetivos
   - Polling de estado (velocidad, inclinación, ventiladores)

2. **uart_rx_task** (Prioridad 7, Stack 4KB)
   - Recepción continua de bytes UART
   - Parser byte-a-byte con destuffing
   - Procesamiento de respuestas

3. **watchdog_task** (Prioridad 5, Stack 2KB)
   - Verifica timeouts de comandos pendientes
   - Reenvía comandos que exceden 100ms sin respuesta
   - Detecta desconexión (1 segundo sin respuestas)

### Cola de Comandos Pendientes

```c
typedef struct {
    cm_frame_t frame;           // Trama a enviar
    uint8_t retry_count;        // Intentos realizados
    int64_t timestamp_us;       // Timestamp del último envío
    bool active;                // Slot ocupado
    bool waiting_ack;           // Espera ACK (true) o RSP (false)
} pending_cmd_t;

#define MAX_PENDING_CMDS 4
```

Cada comando enviado se agrega a la cola y permanece allí hasta:
- Recibir ACK/NAK o RSP correspondiente
- Exceder 3 reintentos
- Timeout de 100ms sin respuesta

## Sistema de Comandos

### Comandos de Control (SET)

**SET_SPEED**:
```c
esp_err_t cm_master_set_speed(float speed_kmh);
```
- Rango: 0.0 - 19.5 km/h
- Formato: uint16 (km/h × 100)
- Espera: ACK/NAK
- Ejemplo: 10.5 km/h → 0x0422 (1050)

**SET_INCLINE**:
```c
esp_err_t cm_master_set_incline(float incline_pct);
```
- Rango: 0.0 - 15.0 %
- Formato: uint16 (% × 10)
- Espera: ACK/NAK
- Ejemplo: 12.5 % → 0x007D (125)

**SET_FAN_STATE**:
```c
esp_err_t cm_master_set_fan(uint8_t fan_id, uint8_t state);
```
- fan_id: 0x01=HEAD, 0x02=CHEST
- state: 0=OFF, 1=LOW, 2=HIGH
- Espera: ACK/NAK

**SET_RELAY**:
```c
esp_err_t cm_master_set_relay(uint8_t relay_id, uint8_t state);
```
- relay_id: 0x01=WAX_PUMP
- state: 0=OFF, 1=ON
- Espera: ACK/NAK

### Comandos de Consulta (GET)

**Heartbeat** (automático cada 300ms):
- `GET_STATUS` → RSP_STATUS (bitmap de estado)
- `GET_SENSOR_SPEED` → RSP_SENSOR_SPEED (velocidad real)
- `GET_INCLINE_POSITION` → RSP_INCLINE_POSITION (inclinación real)
- `GET_FAN_STATE` → RSP_FAN_STATE (estado ventiladores)

**Resultado**:
Los valores recibidos se almacenan en variables globales accesibles vía:
```c
float cm_master_get_real_speed(void);
float cm_master_get_real_incline(void);
uint8_t cm_master_get_head_fan_state(void);
uint8_t cm_master_get_chest_fan_state(void);
```

### Comandos Especiales

**CALIBRATE_INCLINE**:
```c
esp_err_t cm_master_calibrate_incline(void);
```
Inicia rutina de homing en el esclavo (baja inclinación a 0% hasta activar fin de carrera).

**EMERGENCY_STOP**:
Implementado implícitamente: cuando `g_target_speed_kmh = 0`, el maestro envía `SET_SPEED` con valor 0.

## Sistema de Reintentos y Timeouts

### Parámetros

```c
#define CM_MASTER_TIMEOUT_MS      100   // Timeout para ACK/RSP
#define CM_MASTER_MAX_RETRIES     3     // Máximo 3 reintentos
#define CM_MASTER_WATCHDOG_MS     50    // Ciclo de watchdog
#define CM_MASTER_CONN_TIMEOUT_MS 1000  // Sin respuestas = desconectado
```

### Flujo de Reintento

```
Comando enviado → Timer start (100ms)
                     ↓
              ┌──────┴──────┐
              │             │
         Respuesta OK   Timeout (100ms)
              │             │
         Remover        Retry count++
          de cola           │
                      ┌─────┴─────┐
                      │           │
                  < 3 retries   ≥ 3 retries
                      │           │
                  Reenviar    Marcar FAIL
                               Remover cola
```

### Detección de Desconexión

El watchdog task monitorea `g_last_response_us`:
```c
if ((now_us - g_last_response_us) > CM_MASTER_CONN_TIMEOUT_MS * 1000) {
    g_connected = false;
    // Limpiar cola de pendientes
}
```

Estado accesible via:
```c
bool cm_master_is_connected(void);
```

## Estados y Detección de Conexión

### Variables de Estado

```c
static bool g_connected = false;                // Estado de conexión
static float g_target_speed_kmh = 0.0f;         // Objetivo desde UI
static float g_target_incline_pct = 0.0f;       // Objetivo desde UI
static float g_real_speed_kmh = 0.0f;           // Último valor del sensor
static float g_real_incline_pct = 0.0f;         // Última posición
static uint8_t g_head_fan_state = 0;            // 0/1/2
static uint8_t g_chest_fan_state = 0;           // 0/1/2
```

### Sincronización con UI

El maestro lee objetivos desde `g_target_speed_kmh` y `g_target_incline_pct`, que son actualizados por la UI:

```c
// En ui.c o treadmill_state.c
extern float g_target_speed_kmh;    // Usado por cm_master
extern float g_target_incline_pct;  // Usado por cm_master

// La UI actualiza estos valores
g_target_speed_kmh = new_speed;
// El maestro detecta el cambio y envía SET_SPEED
```

## API Pública

### Inicialización

```c
esp_err_t cm_master_init(void);
esp_err_t cm_master_start(void);
```

**Uso típico**:
```c
// En main.c
ret = cm_master_init();
if (ret == ESP_OK) {
    cm_master_start();
}
```

### Control de Velocidad e Inclinación

```c
esp_err_t cm_master_set_speed(float speed_kmh);        // 0-19.5 km/h
esp_err_t cm_master_set_incline(float incline_pct);    // 0-15%
esp_err_t cm_master_calibrate_incline(void);           // Homing
```

### Control de Ventiladores y Relés

```c
esp_err_t cm_master_set_fan(uint8_t fan_id, uint8_t state);
esp_err_t cm_master_set_relay(uint8_t relay_id, uint8_t state);
```

### Lectura de Estado

```c
bool cm_master_is_connected(void);
float cm_master_get_real_speed(void);
float cm_master_get_real_incline(void);
uint8_t cm_master_get_head_fan_state(void);
uint8_t cm_master_get_chest_fan_state(void);
```

## Ejemplos de Uso

### Ejemplo 1: Inicialización

```c
#include "cm_master.h"

void app_main(void) {
    // Inicializar módulo
    esp_err_t ret = cm_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cm_master_init() failed");
        return;
    }

    // Iniciar tareas
    ret = cm_master_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cm_master_start() failed");
    }

    // El heartbeat se ejecuta automáticamente cada 300ms
}
```

### Ejemplo 2: Establecer Velocidad

```c
// Desde la UI o button handler
void on_speed_up_button_press(void) {
    float current_speed = cm_master_get_real_speed();
    float new_speed = current_speed + 0.5f;

    if (new_speed <= 19.5f) {
        esp_err_t ret = cm_master_set_speed(new_speed);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Speed set to %.1f km/h", new_speed);
        } else {
            ESP_LOGW(TAG, "Failed to set speed (queue full or disconnected)");
        }
    }
}
```

### Ejemplo 3: Control de Ventiladores

```c
// Toggle del ventilador de cabeza
void toggle_head_fan(void) {
    uint8_t current_state = cm_master_get_head_fan_state();
    uint8_t new_state = (current_state + 1) % 3;  // Ciclar 0→1→2→0

    cm_master_set_fan(0x01, new_state);  // 0x01 = HEAD fan

    const char *state_str[] = {"OFF", "LOW", "HIGH"};
    ESP_LOGI(TAG, "Head fan: %s", state_str[new_state]);
}
```

### Ejemplo 4: Verificar Conexión

```c
// En ui_update_task
void ui_update_task(void *pvParameter) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (cm_master_is_connected()) {
            float real_speed = cm_master_get_real_speed();
            float real_incline = cm_master_get_real_incline();

            // Actualizar UI con valores reales
            update_display_speed(real_speed);
            update_display_incline(real_incline);
        } else {
            // Mostrar alerta de desconexión
            show_disconnected_warning();
        }
    }
}
```

## Logs y Debugging

### Etiquetas de Log

- `CM_MASTER`: Eventos principales del maestro

### Mensajes Clave

**Conexión exitosa**:
```
I (xxxx) CM_MASTER: Master started successfully
I (xxxx) CM_MASTER: Connected to slave
```

**Envío de comandos**:
```
I (xxxx) CM_MASTER: Sending SET_SPEED: 10.5 km/h (0x0422)
I (xxxx) CM_MASTER: Sent frame: CMD=0x11, SEQ=42, LEN=2
```

**Recepción de respuestas**:
```
I (xxxx) CM_MASTER: ACK received for SEQ=42
I (xxxx) CM_MASTER: RSP_SENSOR_SPEED: 10.3 km/h
```

**Errores**:
```
W (xxxx) CM_MASTER: Pending queue full! Cannot send CMD=0x11
W (xxxx) CM_MASTER: Timeout for SEQ=42, retry 1/3
E (xxxx) CM_MASTER: Command SEQ=42 failed after 3 retries
W (xxxx) CM_MASTER: Slave disconnected (no responses for 1000ms)
```

## Troubleshooting

### Cola de Pendientes Llena

**Síntoma**: Log `"Pending queue full!"`
**Causa**: Más de 4 comandos sin respuesta simultáneamente
**Solución**:
- Verificar conexión física RS485
- Confirmar que el esclavo está respondiendo
- Aumentar `MAX_PENDING_CMDS` si es necesario

### Timeouts Constantes

**Síntoma**: Log `"Timeout for SEQ=X, retry Y/3"`
**Causa**: Comandos no reciben respuesta a tiempo
**Soluciones**:
1. Verificar cableado RS485 (A, B, GND)
2. Confirmar baudrate (115200) en ambos lados
3. Revisar que el esclavo está ejecutando
4. Medir señal con osciloscopio

### Desconexión Frecuente

**Síntoma**: Log `"Slave disconnected"`
**Causa**: Más de 1 segundo sin respuestas
**Soluciones**:
- Revisar alimentación del módulo Base
- Verificar reset del esclavo en logs
- Comprobar integridad del cable RS485

### Comandos Ignorados

**Síntoma**: Comandos enviados pero no ejecutados
**Causa**: Esclavo responde NAK
**Soluciones**:
- Ver código de error NAK en logs
- `0xE5`: Esclavo no calibrado → enviar CALIBRATE_INCLINE
- `0xE6`: VFD en fallo → verificar hardware VFD

## Referencias

- **CM_Protocol v2.1**: Ver `common_components/cm_protocol/README.md`
- **Módulo Base**: Ver `Base/README.md`
- **Implementación**: `Consola/main/cm_master.c`

---

**Última actualización**: 2025-11-05
