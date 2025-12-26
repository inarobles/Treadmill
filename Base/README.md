# Base - Sistema de Control de Cinta de Correr (ESP32)

Sistema embebido completo para el control de una cinta de correr profesional basado en ESP32. Este dispositivo actúa como **ESCLAVO** en el protocolo de comunicación **SYNC simplificado**, gestionando todos los aspectos de bajo nivel del hardware de la cinta.

## Versión Actual: FASE 9 - Integración de Hardware (v6)

### Cambios Críticos en esta Versión:
- **Protocolo**: Migración de `CM_Protocol v2.1` binario a un protocolo **ASCII SYNC/DATA** más robusto y fácil de depurar.
- **Sensor de Velocidad**: Sensor Hall **DESHABILITADO** (GPIO 15) por ruido electromagnético excesivo. La velocidad real ahora se estima directamente desde la frecuencia de salida del VFD.
- **Pinout v6**: Reasignación completa de pines para mejorar el ruteado y estabilidad.
- **Homing Dinámico**: Calibración de inclinación con timeout basado en la última posición conocida guardada en NVS.

## Descripción General

Este proyecto implementa un sistema de control distribuido donde el ESP32 se encarga de:
- Control de velocidad del motor principal mediante VFD SU300 (Modbus RTU).
- Control de inclinación con calibración automática y recuperación de posición vía NVS.
- Gestión de ventiladores con velocidades variables (Head/Chest).
- Control de bomba de cera con temporización automática de 5 segundos.
- Comunicación UART bidireccional continua con la consola principal.
- Sistema de seguridad con watchdog de 1 segundo y detección de fallos de sensor.

## Hardware

### Microcontrolador
- **MCU**: ESP32 (dual-core, 240MHz)
- **Framework**: ESP-IDF v5.x
- **Persistencia**: Almacenamiento NVS para posición de inclinación y errores críticos.

### Interfaces de Comunicación
- **Consola (UART)**: Protocolo SYNC (ASCII) sobre conexión directa de 3 hilos (TX, RX, GND)
  - UART1 @ 115200 baud, 8N1
  - TX: GPIO 17, RX: GPIO 16
- **VFD SU300 (Modbus RTU)**:
  - UART2 @ 9600 baud, 8N1
  - TX: GPIO 19, RX: GPIO 18

### Periféricos Controlados (Pinout v6)

| Periférico | Función | Pin | Notas |
|------------|---------|-----|-------|
| VFD | Run/Stop/Freq | Modbus | Registro 0x2000, 0x2001 |
| Inclinación | ON/OFF | **GPIO 33** | Relé 4 |
| Inclinación | Dirección | **GPIO 32** | Relé 5 (NC=Up, NO=Down) |
| Inclinación | Fin Carrera | **GPIO 21** | Entrada con pull-up interno |
| Ventilador Head | ON/OFF | **GPIO 26** | Relé 6 |
| Ventilador Head | Speed | **GPIO 27** | Relé 7 |
| Ventilador Chest| ON/OFF | **GPIO 14** | Relé 2 |
| Ventilador Chest| Speed | **GPIO 13** | Relé 1 |
| Bomba Cera | Relé | **GPIO 25** | Relé 3, tempo 5s |

## Arquitectura del Software

### Tareas FreeRTOS

1. **uart_rx_task** (Prioridad 10)
   - Parser ASCII de líneas terminadas en `\n`.
   - Procesa comandos `SYNC` y `CALIBRATE_INCLINE`.
2. **vfd_control_task** (Prioridad 8)
   - Bucle de control Modbus cada 500ms.
   - Monitoriza fallos (registro 0x2100).
3. **incline_control_task** (Prioridad 7)
   - Control de máquina de estados de inclinación.
   - Maneja Homing, Up, Down y protección contra fallos del sensor.
4. **speed_update_task** (Prioridad 5)
   - Estima la velocidad real desde el VFD cada 500ms.
5. **watchdog_task** (Prioridad 6)
   - Timeout de 1000ms. Si se pierde el flujo de `SYNC`, entra en **SAFE STATE**.

## Protocolo de Comunicación (ASCII SYNC)

El sistema utiliza un protocolo de "sincronización continua". La consola envía sus objetivos y la base responde con el estado real.

### Maestro → Esclavo (cada 100ms)
`SYNC=speed,incline,fan_head,fan_chest,wax,training_mode\n`
- `speed`: float (km/h)
- `incline`: float (%)
- `fan_head/chest`: int (0=OFF, 1=LOW, 2=HIGH)
- `wax`: int (1 para activar pulso de 5s)
- `training_mode`: int (0/1. Si es 0, la velocidad se fuerza a 0 por seguridad).

### Esclavo → Maestro (Respuesta al SYNC)
`DATA=speed,incline,vfd_freq,vfd_fault,fan_head,fan_chest,incline_fault\n`
- `speed`: Velocidad real estimada desde el VFD.
- `incline`: Posición actual calculada.
- `vfd_freq`: Frecuencia real en Hz del VFD.
- `vfd_fault`: 1 si el VFD reporta fallo o desconexión.
- `incline_fault`: 1 si el fin de carrera no fue detectado en el homing o se sobrepasó el límite.

## Lógica de Inclinación

- **Homing Dinámico**: Al arrancar, si no detecta error, inicia descenso. El timeout se calcula según la última posición guardada en NVS.
- **Protección de Seguridad**: Si el motor baja más allá de -2% teóricos sin tocar el fin de carrera, se declara `incline_fault` y se bloquea el sistema.
- **Retorno a Cero**: Al desactivar el `training_mode`, el sistema vuelve automáticamente a 0% e inicia un homing de recalibración.

## Control de Velocidad (VFD SU300)

Debido a la desconexión del sensor Hall, la velocidad se calcula según:
`real_speed_kmh = vfd_freq_hz × 0.128`

**Relación de control**:
`vfd_freq_hz = target_kmh × 7.8125` (Calibrado para 20 km/h @ 156.25 Hz).

## Compilación y Flasheo

```bash
cd Base
idf.py build
idf.py -p COMx flash monitor
```

---
**Última actualización**: 2025-12-26 | **Autor**: Proyecto Treadmill
