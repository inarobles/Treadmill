# Sala de Máquinas - Sistema Esclavo

Proyecto ESP32 para el control de la sala de máquinas de la cinta de correr.
Este dispositivo actúa como **ESCLAVO** en el protocolo CM_Protocol_v2.1.

## Hardware

- **MCU**: ESP32 (dual-core, 240MHz)
- **Comunicación**: RS485 (via MAX485)
- **Interfaces**:
  - Modbus RTU (VFD control)
  - GPIO (relés de inclinación)
  - Sensor de velocidad

## Estructura del Proyecto

```
Sala_Maquinas/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
└── main/
    ├── CMakeLists.txt
    └── main.c             # Entry point
```

## Fases de Implementación

- [x] **FASE 2**: Proyecto base (compila y arranca)
- [x] **FASE 3**: UART básico (recepción de bytes)
- [ ] **FASE 5**: Parser de protocolo (procesar comandos)
- [ ] **FASE 6**: Respuestas al Maestro
- [ ] **FASE 7**: Simulación de velocidad
- [ ] **FASE 11**: Control de hardware real (VFD, relés, sensores)

## Configuración de Pines

### RS485 (UART2)
- **TX**: GPIO 16
- **RX**: GPIO 17
- **Baudrate**: 115200
- **Control DE/RE**: Hardware automático (MAX485)

## Compilar y Flashear

```bash
cd C:\esp\Consola_Cinta\Sala_Maquinas
idf.py build
idf.py -p COMx flash monitor
```

## Velocidad de Simulación

- **Velocidad inicial**: 0 km/h
- **Velocidad máxima**: 19.5 km/h
- **Rampa de aceleración**: 0.5 km/h/segundo
- **Rampa de deceleración**: 0.5 km/h/segundo

## Protocolo

Ver [cm_protocol](../common_components/cm_protocol/README.md) para detalles del protocolo.

## Notas

- El watchdog de seguridad (500ms) se implementará en FASE 9
- El control del VFD se implementará en FASE 11
- Los relés de inclinación se implementarán en FASE 11
