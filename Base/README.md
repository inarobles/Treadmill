# Base - Sistema de Control de Cinta de Correr (ESP32)

Sistema embebido completo para el control de una cinta de correr profesional basado en ESP32. Este dispositivo actúa como **ESCLAVO** en el protocolo de comunicación CM_Protocol v2.1, gestionando todos los aspectos de bajo nivel del hardware de la cinta.

## Descripción General

Este proyecto implementa un sistema de control distribuido donde el ESP32 se encarga de:
- Control de velocidad del motor principal mediante VFD (Variador de Frecuencia)
- Lectura de sensores de velocidad en tiempo real
- Control de inclinación con calibración automática (homing)
- Gestión de ventiladores con velocidades variables
- Control de bomba de cera con temporización automática
- Comunicación RS485 con la consola principal
- Sistema de seguridad con watchdog y estado de emergencia

## Hardware

### Microcontrolador
- **MCU**: ESP32 (dual-core, 240MHz)
- **Framework**: ESP-IDF v5.x
- **RTOS**: FreeRTOS (integrado en ESP-IDF)

### Interfaces de Comunicación
- **RS485**: Comunicación con consola principal (protocolo CM_Protocol v2.1)
  - UART1 @ 115200 baud
  - TX: GPIO 17, RX: GPIO 16

- **Modbus RTU**: Control del VFD SU300
  - UART2 @ 9600 baud, 8N1
  - TX: GPIO 19, RX: GPIO 18
  - Hardware con control automático de dirección (MAX485)

### Periféricos Controlados
1. **VFD (Variador de Frecuencia) SU300**
   - Control mediante Modbus RTU
   - Rango: 0-60 Hz (equivalente a 0-20 km/h)
   - Detección automática de fallos

2. **Sensor de Velocidad**
   - Entrada: GPIO 34 (solo entrada)
   - Periférico PCNT (Pulse Counter)
   - Pull-down habilitado para evitar lecturas flotantes
   - Filtro de glitch: 1µs

3. **Motor de Inclinación**
   - Relé UP: GPIO 27
   - Relé DOWN: GPIO 14
   - Fin de carrera: GPIO 35 (con pull-up)
   - Velocidad: 0.05%/segundo

4. **Ventiladores** (2 unidades, 2 velocidades cada uno)
   - Ventilador Cabeza: ON/OFF (GPIO 26), SPEED (GPIO 25)
   - Ventilador Pecho: ON/OFF (GPIO 33), SPEED (GPIO 32)

5. **Bomba de Cera**
   - Relé: GPIO 13
   - Temporización automática: 5 segundos

## Arquitectura del Software

### Estructura del Proyecto

```
Base/
├── CMakeLists.txt              # Configuración principal del proyecto
├── sdkconfig                   # Configuración de ESP-IDF
├── dependencies.lock           # Dependencias bloqueadas
├── README.md                   # Este archivo
├── main/
│   ├── CMakeLists.txt          # Componentes del main
│   ├── main.c                  # Punto de entrada y lógica principal
│   ├── vfd_driver.h            # API del driver VFD
│   ├── vfd_driver.c            # Implementación control VFD Modbus
│   ├── speed_sensor.h          # API del sensor de velocidad
│   └── speed_sensor.c          # Implementación PCNT
└── components/
    └── esp-modbus/             # Stack Modbus RTU de Espressif
```

### Dependencias

El proyecto utiliza los siguientes componentes:

**Componentes Propios:**
- `cm_protocol`: Protocolo de comunicación RS485 (../common_components/cm_protocol)
  - Framing con byte stuffing (SOF: 0x3A, ESC: 0x7D)
  - CRC-16/CCITT-FALSE para integridad
  - Parser byte-a-byte resistente a ruido

**Componentes ESP-IDF:**
- `driver`: Drivers de periféricos (UART, GPIO, PCNT)
- `nvs_flash`: Almacenamiento no volátil
- `esp_timer`: Temporizadores de alta precisión
- `freertos`: Sistema operativo en tiempo real
- `esp-modbus`: Stack Modbus Master RTU

### Tareas FreeRTOS

El sistema está organizado en 5 tareas principales:

1. **uart_rx_task** (Prioridad 10, Stack 4KB)
   - Recepción de comandos por RS485
   - Parser de protocolo CM_Protocol v2.1
   - Timeout de trama: 100ms
   - Procesa comandos del maestro

2. **vfd_control_task** (Prioridad 8, Stack 4KB)
   - Control del VFD por Modbus RTU
   - Frecuencia de actualización: 200ms
   - Monitorización de fallos (registro 0x2104)
   - Conversión km/h → Hz: (kph/20) × 60

3. **speed_update_task** (Prioridad 5, Stack 4KB)
   - Lectura de pulsos del sensor PCNT
   - Actualización cada 500ms
   - Cálculo de velocidad real: `pulsos/seg × factor_calibración`
   - Factor de calibración: 0.00875 (configurable)

4. **incline_control_task** (Prioridad 7, Stack 4KB)
   - Control de posición de inclinación
   - Ciclo: 50ms
   - Estados: STOPPED, HOMING, UP, DOWN
   - Calibración automática al inicio (homing)

5. **watchdog_task** (Prioridad 6, Stack 2KB)
   - Supervisión de comunicación
   - Timeout: 700ms sin comandos
   - Activa estado de seguridad si se pierde comunicación

### Sistema de Seguridad

#### Estado de Emergencia (Safe State)
Se activa automáticamente cuando:
- No se reciben comandos del maestro en 700ms (watchdog)
- Se recibe comando `CM_CMD_EMERGENCY_STOP`
- Se detecta fallo crítico

**Acciones en Safe State:**
- Parada inmediata del VFD
- Velocidad objetivo = 0 km/h
- Inclinación objetivo = 0%
- Apagado de todos los ventiladores
- Apagado de bomba de cera
- Motor de inclinación detenido

#### Protección VFD
- Rechazo de comandos si VFD está en fallo (`NAK_VFD_FAULT`)
- Rechazo de comandos si VFD desconectado
- Monitorización continua del registro de fallos

## Protocolo de Comunicación

### CM_Protocol v2.1

**Formato de Trama:**
```
Trama física:   [SOF] [stuffed_data...]
Trama lógica:   [LEN] [SEQ] [CMD] [PAYLOAD...] [CRC_H] [CRC_L]
```

### Comandos Soportados (Maestro → Esclavo)

| Comando | Código | Payload | Descripción |
|---------|--------|---------|-------------|
| `SET_SPEED` | 0x11 | 2 bytes (km/h × 100) | Establecer velocidad objetivo |
| `SET_INCLINE` | 0x12 | 2 bytes (% × 10) | Establecer inclinación objetivo |
| `SET_FAN_STATE` | 0x14 | 2 bytes (ID, Estado) | Control de ventilador (0=OFF, 1=LOW, 2=HIGH) |
| `SET_RELAY` | 0x13 | 2 bytes (ID, Estado) | Control de relés (ID=1: Bomba de cera) |
| `CALIBRATE_INCLINE` | 0x15 | - | Iniciar calibración (homing) |
| `EMERGENCY_STOP` | 0x1F | - | Parada de emergencia |
| `GET_STATUS` | 0x22 | - | Solicitar estado general |
| `GET_SENSOR_SPEED` | 0x21 | - | Solicitar velocidad real |
| `GET_INCLINE_POSITION` | 0x23 | - | Solicitar posición de inclinación |
| `GET_FAN_STATE` | 0x24 | - | Solicitar estado de ventiladores |

### Respuestas (Esclavo → Maestro)

| Respuesta | Código | Payload | Descripción |
|-----------|--------|---------|-------------|
| `ACK` | 0x80 | 1 byte (eco SEQ) | Confirmación exitosa |
| `NAK` | 0x81 | 2 bytes (SEQ, error) | Rechazo con código de error |
| `STATUS` | 0xA2 | 1 byte (bitmap) | Estado: bit 0 = VFD fault |
| `SENSOR_SPEED` | 0xA1 | 2 bytes (km/h × 100) | Velocidad medida |
| `INCLINE_POSITION` | 0xA3 | 2 bytes (% × 10) | Posición actual |
| `FAN_STATE` | 0xA4 | 2 bytes (head, chest) | Estado de ventiladores |

### Códigos de Error (NAK)

| Código | Nombre | Descripción |
|--------|--------|-------------|
| 0xE2 | `UNKNOWN_CMD` | Comando no reconocido |
| 0xE3 | `INVALID_PAYLOAD` | Payload inválido o fuera de rango |
| 0xE4 | `BUSY` | Sistema ocupado |
| 0xE5 | `NOT_READY` | Sistema no calibrado |
| 0xE6 | `VFD_FAULT` | VFD en fallo o desconectado |

## Control del VFD SU300

### Configuración Modbus

El driver configura automáticamente los siguientes parámetros del VFD:

- **F0-01 = 2**: Fuente de comando = RS485
- **F0-02 = 9**: Fuente de frecuencia = Comunicación

### Registros Modbus Utilizados

| Registro | Dirección | Tipo | Función |
|----------|-----------|------|---------|
| Control | 0x2000 | W | Comando RUN/STOP (1=RUN_FWD, 5=STOP) |
| Frecuencia | 0x2001 | W | Frecuencia objetivo (Hz × 100) |
| Código de Fallo | 0x2104 | R | Estado de fallo (0 = OK) |

### Conversión de Velocidad

```
Frecuencia (Hz) = (Velocidad (km/h) / 20) × 60
Valor Modbus = Frecuencia (Hz) × 100
```

Ejemplo: 10 km/h → 30 Hz → Valor 3000

## Sensor de Velocidad

### Configuración PCNT (Pulse Counter)

- **GPIO**: 34 (solo entrada, con pull-down)
- **Modo**: Incremento en flanco ascendente
- **Filtro**: Glitch < 1µs descartado
- **Límites**: -1000 a +1000 pulsos

### Cálculo de Velocidad

```c
pulsos_por_segundo = pulsos_acumulados / tiempo_transcurrido
velocidad_kmh = pulsos_por_segundo × factor_calibración
```

**Factor de calibración actual**: 0.00875
- Derivado de: 10.5 km/h = 1200 pulsos/seg
- **NOTA**: Este factor debe ajustarse según el sensor real instalado

## Control de Inclinación

### Máquina de Estados

```
HOMING → STOPPED → UP/DOWN → STOPPED
   ↑                           ↓
   └───────────────────────────┘
```

1. **HOMING**: Calibración inicial
   - Baja hasta activar fin de carrera (GPIO 35)
   - Establece posición 0%
   - **TEMPORAL**: Completado inmediatamente (sensor desconectado)

2. **STOPPED**: Motor detenido
   - Si error > 0.1%, transiciona a UP o DOWN

3. **UP/DOWN**: Motor en movimiento
   - Velocidad: 0.05%/segundo
   - Se detiene al alcanzar objetivo

### Configuración Temporal

**IMPORTANTE**: El sensor de fin de carrera está actualmente deshabilitado en el código debido a hardware no conectado. Se ha configurado GPIO 35 con pull-up para simular sensor siempre en alto.

Para habilitar el sensor, descomentar las secciones marcadas con `// TEMPORAL` en `main.c:554-565` y `main.c:581-587`.

## Control de Ventiladores

Cada ventilador tiene 3 estados:
- **0**: Apagado (ambos relés OFF)
- **1**: Velocidad baja (ON=1, SPEED=0)
- **2**: Velocidad alta (ON=1, SPEED=1)

## Bomba de Cera

Al recibir comando `SET_RELAY` con ID=1 y Estado=1:
1. Activa relé GPIO 13
2. Inicia temporizador de 5 segundos
3. Callback automático apaga el relé

El estado se puede forzar a OFF enviando Estado=0.

## Compilación y Flasheo

### Requisitos

- ESP-IDF v5.5 o superior
- Python 3.x
- Toolchain Xtensa para ESP32

### Comandos

```bash
# Navegar al directorio del proyecto
cd /home/user/Treadmill/Base

# Configurar (opcional, sdkconfig ya está presente)
idf.py menuconfig

# Compilar
idf.py build

# Flashear y monitorear
idf.py -p /dev/ttyUSB0 flash monitor

# Solo monitorear
idf.py -p /dev/ttyUSB0 monitor
```

### Configuración del Puerto

Reemplazar `/dev/ttyUSB0` con el puerto correcto:
- **Linux**: `/dev/ttyUSB0`, `/dev/ttyACM0`
- **macOS**: `/dev/cu.usbserial-*`
- **Windows**: `COM3`, `COM4`, etc.

## Configuración de Pines (Resumen)

| Función | GPIO | Tipo | Notas |
|---------|------|------|-------|
| RS485 TX | 17 | O | UART1, comunicación con consola |
| RS485 RX | 16 | I | UART1, comunicación con consola |
| VFD TX | 19 | O | UART2, Modbus RTU |
| VFD RX | 18 | I | UART2, Modbus RTU |
| Sensor Velocidad | 34 | I | PCNT, pull-down |
| Fin Carrera Inclinación | 35 | I | Pull-up (temporal: sensor desconectado) |
| Relé Inclinación UP | 27 | O | Control motor inclinación |
| Relé Inclinación DOWN | 14 | O | Control motor inclinación |
| Relé Bomba Cera | 13 | O | Temporización 5s |
| Ventilador Cabeza ON | 26 | O | Relé 6 |
| Ventilador Cabeza SPEED | 25 | O | Relé 7 |
| Ventilador Pecho ON | 33 | O | Relé 4 |
| Ventilador Pecho SPEED | 32 | O | Relé 5 |

## Parámetros Configurables

### En `main.c`

```c
// Calibración del sensor de velocidad
#define g_calibration_factor 0.00875  // Ajustar según sensor real

// Intervalo de actualización de velocidad
#define SPEED_UPDATE_INTERVAL_MS 500

// Timeout del watchdog
#define WATCHDOG_TIMEOUT_US (700 * 1000)  // 700ms

// Velocidad de inclinación
#define INCLINE_SPEED_PCT_PER_MS (0.05f / 1000.0f)  // 0.05%/segundo

// Duración de activación de bomba de cera
#define WAX_PUMP_ACTIVATION_DURATION_MS 5000
```

### En `vfd_driver.c`

```c
// Dirección Modbus del VFD
#define VFD_SLAVE_ID 1

// Configuración UART Modbus
#define VFD_BAUD_RATE 9600
#define VFD_PARITY UART_PARITY_DISABLE

// Frecuencia de control del VFD
#define VFD_POLL_MS 200
```

## Logs y Debugging

### Niveles de Log

El sistema genera logs con las siguientes etiquetas:

- `SLAVE`: Eventos principales del sistema
- `VFD_DRIVER_MODBUS`: Operaciones del VFD y Modbus
- `SPEED_SENSOR`: Inicialización y eventos del sensor

### Mensajes Importantes

**Inicialización exitosa:**
```
I (xxxx) SLAVE: Sistema iniciado correctamente
I (xxxx) SLAVE: Esperando comandos del Maestro...
```

**Watchdog activado:**
```
E (xxxx) SLAVE: ¡WATCHDOG TIMEOUT! No se recibió comando en 700 ms
W (xxxx) SLAVE: ⚠️ ENTERING SAFE STATE - Communication lost or emergency stop
```

**VFD configurado:**
```
I (xxxx) VFD_DRIVER_MODBUS: VFD configurado para control por Modbus.
I (xxxx) VFD_DRIVER_MODBUS: Configuración VFD exitosa. Iniciando bucle de control.
```

### Heartbeat

El sistema genera un mensaje cada 10 segundos mostrando el estado:
```
I (xxxxx) SLAVE: Heartbeat #X - Speed: R/T km/h, Incline: R/T %
```
Donde R = real (actual) y T = target (objetivo)

## Estado del Proyecto

### Implementado ✅

- [x] Proyecto base ESP-IDF compilable
- [x] UART para RS485 (115200 baud)
- [x] Parser de protocolo CM_Protocol v2.1
- [x] Procesamiento de todos los comandos
- [x] Respuestas ACK/NAK
- [x] Control de VFD por Modbus RTU
- [x] Lectura de sensor de velocidad (PCNT)
- [x] Control de inclinación con estados
- [x] Control de ventiladores (2 velocidades)
- [x] Control de bomba de cera con timer
- [x] Sistema de watchdog (700ms)
- [x] Estado de seguridad (safe state)
- [x] Monitorización de fallos del VFD

### Pendiente / Notas

- [ ] **Calibración del sensor de velocidad**: El factor `0.00875` es placeholder
- [ ] **Fin de carrera de inclinación**: Actualmente deshabilitado (hardware no conectado)
- [ ] **Pruebas de integración completas**: Requiere hardware físico conectado
- [ ] **Parámetros del VFD**: Confirmar F5-00 (baudrate) y F5-02 (paridad) en hardware

## Documentación Relacionada

- **CM_Protocol v2.1**: Ver [../common_components/cm_protocol/README.md](../common_components/cm_protocol/README.md)
- **Manual VFD SU300**: Registros Modbus y configuración de parámetros
- **ESP-IDF Programming Guide**: https://docs.espressif.com/projects/esp-idf/

## Solución de Problemas

### El VFD no responde

1. Verificar cableado TX/RX (GPIO 19/18)
2. Confirmar baudrate del VFD (parámetro F5-00 = 9600)
3. Verificar ID Modbus del VFD (debe ser 1, parámetro F0-00)
4. Revisar logs: `VFD_DRIVER_MODBUS: Error al escribir en registro...`

### Velocidad siempre reporta 0

1. Verificar conexión del sensor al GPIO 34
2. Confirmar que el sensor genera pulsos (medir con osciloscopio)
3. Ajustar `g_calibration_factor` según especificaciones del sensor
4. Verificar que la tarea `speed_update_task` está corriendo

### Watchdog se activa constantemente

1. Verificar comunicación RS485 (GPIO 16/17)
2. Confirmar que la consola está enviando comandos periódicamente (< 700ms)
3. Revisar niveles de voltaje en bus RS485
4. Incrementar `WATCHDOG_TIMEOUT_US` temporalmente para debug

### Inclinación no se mueve

1. Verificar que `g_incline_is_calibrated = true` en logs
2. Confirmar que el comando `SET_INCLINE` recibe ACK
3. Verificar cableado de relés (GPIO 27 y 14)
4. Revisar que `g_emergency_state = false`

## Licencia

Este proyecto es parte del sistema de control de cinta de correr. Uso interno.

## Autor

Desarrollado para el proyecto Treadmill.

---

**Versión del Firmware**: FASE 9 - Integración de Hardware
**Fecha de última actualización**: 2025-11-05
