# Consola - Sistema de Control de Cinta de Correr (ESP32-P4)

Sistema de consola interactiva completo para control de cinta de correr profesional, basado en ESP32-P4 con pantalla táctil de 7" y múltiples conectividades.

![ESP32-P4](https://img.shields.io/badge/ESP32--P4-Function%20EV%20Board-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3%2B-green)
![LVGL](https://img.shields.io/badge/LVGL-v8-orange)

## Descripción General

Este proyecto implementa la consola MAESTRO del sistema de control distribuido de una cinta de correr. La consola actúa como interfaz de usuario principal y coordinador del sistema, comunicándose con el módulo Base (esclavo) mediante RS485.

### Características Principales

- **Pantalla Táctil**: 7" 1024x600 MIPI-DSI con interfaz gráfica LVGL
- **Comunicación RS485**: Protocolo maestro CM_Protocol v2.1 para control de hardware
- **Conectividad WiFi**: Gestión de redes WiFi via ESP-Hosted (ESP32-C6)
- **Bluetooth LE**: Cliente para monitores de frecuencia cardíaca
- **Sistema de Audio**: Reproducción de audio para eventos
- **Botones Físicos**: 13 botones para control directo
- **Gestión de Entrenamientos**: Múltiples modos de entrenamiento con seguimiento
- **Persistencia**: Almacenamiento NVS para configuración y estado

## Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-P4 (Maestro)                       │
│  ┌─────────────┐  ┌──────────┐  ┌────────────┐            │
│  │   UI Task   │  │ CM Master│  │ WiFi/BLE   │            │
│  │   (LVGL)    │──│  Task    │  │   Tasks    │            │
│  └─────────────┘  └──────────┘  └────────────┘            │
│         │              │               │                    │
│         v              v               v                    │
│  ┌─────────────────────────────────────────────┐           │
│  │        Treadmill State (Mutex Protected)    │           │
│  └─────────────────────────────────────────────┘           │
│         │              │               │                    │
└─────────┼──────────────┼───────────────┼────────────────────┘
          │              │               │
          v              v               v
    Display       UART1 (RS485)   ESP32-C6 (ESP-Hosted)
    Touch                │               │
    Audio                v               v
    Buttons         Base Module    WiFi/BLE Radio
                    (Esclavo)
```

## Documentación Detallada

El proyecto está documentado en módulos independientes:

- **[HARDWARE.md](docs/HARDWARE.md)**: Especificaciones de hardware, conexiones y periféricos
- **[COMUNICACION_RS485.md](docs/COMUNICACION_RS485.md)**: Protocolo maestro CM_Protocol v2.1
- **[WIFI.md](docs/WIFI.md)**: Sistema WiFi con ESP-Hosted, escaneo y HTTP
- **[BLE.md](docs/BLE.md)**: Cliente BLE para monitores de frecuencia cardíaca
- **[INTERFAZ_GRAFICA.md](docs/INTERFAZ_GRAFICA.md)**: Sistema UI completo con LVGL v8

## Inicio Rápido

### Requisitos

- ESP-IDF v5.3 o superior
- ESP32-P4-Function-EV-Board
- Pantalla LCD 7" con driver EK79007
- Cable USB-C

### Compilar y Flashear

```bash
# Clonar el proyecto
cd /ruta/al/Treadmill/Consola

# Compilar
idf.py build

# Flashear y monitorear
idf.py -p /dev/ttyUSB0 flash monitor
```

## Estructura del Proyecto

```
Consola/
├── main/
│   ├── main.c                  # Punto de entrada
│   ├── treadmill_state.c/h     # Estado global
│   ├── cm_master.c/h           # Maestro RS485
│   ├── wifi_manager.c/h        # Gestión WiFi
│   ├── wifi_client.c/h         # Cliente WiFi + HTTP
│   ├── ble_client.c/h          # Cliente BLE
│   ├── ui.c/h                  # Interfaz gráfica
│   ├── ui_wifi.c               # Pantallas WiFi
│   ├── button_handler.c/h      # Botones físicos
│   └── audio.c/h               # Sistema de audio
└── docs/                       # Documentación detallada
```

## Subsistemas Principales

### 1. Estado Global (`treadmill_state.c/h`)

Estructura compartida protegida por mutex que contiene:
- Velocidad y objetivo de velocidad
- Inclinación y objetivo de inclinación
- Tiempo transcurrido y distancia
- BPM del monitor de frecuencia cardíaca
- Estado de conexión BLE
- Peso del usuario
- Contador de mantenimiento

### 2. Maestro CM Protocol (`cm_master.c/h`)

Sistema de comunicación RS485 con el módulo Base:
- Heartbeat cada 300ms
- Comandos asíncronos con timeout y retry
- Control de velocidad, inclinación, ventiladores
- Detección de desconexión

Ver detalles en [COMUNICACION_RS485.md](docs/COMUNICACION_RS485.md)

### 3. WiFi (`wifi_manager.c/h`, `wifi_client.c/h`)

Sistema completo de conectividad:
- Escaneo de redes disponibles
- Almacenamiento de credenciales en NVS
- Conexión automática a redes guardadas
- Subida de entrenamientos via HTTP
- Sincronización de hora (SNTP)

Ver detalles en [WIFI.md](docs/WIFI.md)

### 4. BLE (`ble_client.c/h`)

Cliente para monitores de frecuencia cardíaca:
- Escaneo de dispositivos Heart Rate (0x180D)
- Conexión y emparejamiento
- Notificaciones de BPM en tiempo real
- Reconexión automática

Ver detalles en [BLE.md](docs/BLE.md)

### 5. Interfaz Gráfica (`ui.c/h`, `ui_wifi.c`)

UI completa con LVGL v8:
- Pantalla de selección de entrenamiento
- Pantalla principal con métricas
- Entrada de peso del usuario
- Configuración WiFi con teclado virtual
- Avisos de mantenimiento

Ver detalles en [INTERFAZ_GRAFICA.md](docs/INTERFAZ_GRAFICA.md)

## Parámetros Principales

| Parámetro | Valor | Descripción |
|-----------|-------|-------------|
| Velocidad máxima | 19.5 km/h | `MAX_SPEED_KMH` |
| Inclinación máxima | 15% | `MAX_CLIMB_PERCENT` |
| Heartbeat RS485 | 300 ms | `CM_MASTER_HEARTBEAT_MS` |
| Timeout RS485 | 100 ms | `CM_MASTER_TIMEOUT_MS` |
| Baudrate RS485 | 115200 | `CM_MASTER_BAUD_RATE` |
| Peso predeterminado | 70 kg | `DEFAULT_USER_WEIGHT_KG` |

## Solución de Problemas Comunes

### Pantalla no enciende
- Verificar conexión de backlight (GPIO 26)
- Verificar LCD_RST (GPIO 27)
- Revisar alimentación 5V del adaptador

### WiFi no conecta
- Verificar firmware ESP-Hosted en ESP32-C6
- Revisar credenciales guardadas en NVS
- Ver logs de `WIFI_CLIENT`

### BLE no encuentra dispositivos
- Confirmar firmware ESP-Hosted incluye BLE
- Verificar monitor HR en modo pairing
- Ver logs de `NIMBLE_BLE_CLIENT`

### RS485 sin comunicación
- Verificar cableado TX/RX (GPIO 4/5)
- Confirmar módulo Base alimentado
- Verificar baudrate (115200)

Ver más en la documentación específica de cada módulo.

## Estado del Proyecto

### Implementado ✅
- Interfaz gráfica completa
- Maestro CM Protocol
- WiFi y HTTP
- Cliente BLE
- Audio y botones
- Gestión de entrenamientos
- Persistencia NVS
- Contador de mantenimiento

### Por Implementar
- OTA updates
- Gráficos de progreso
- Multi-idioma

## Licencia

Proyecto interno para sistema de control de cinta de correr.

---

**ESP-IDF**: v5.3+ | **LVGL**: v8 | **Última actualización**: 2025-11-05
