# Hardware - ESP32-P4 Consola

Especificaciones completas de hardware, conexiones y periféricos del sistema de consola.

## Índice

- [Especificaciones del MCU](#especificaciones-del-mcu)
- [Pantalla y Touch](#pantalla-y-touch)
- [Módulo ESP-Hosted](#módulo-esp-hosted)
- [Conexiones GPIO](#conexiones-gpio)
- [Sistema de Audio](#sistema-de-audio)
- [Interfaz RS485](#interfaz-rs485)
- [Botones Físicos](#botones-físicos)
- [Alimentación](#alimentación)
- [Esquemático de Conexiones](#esquemático-de-conexiones)

## Especificaciones del MCU

### ESP32-P4 WROOM-1

**Procesador**:
- Arquitectura: Dual-core RISC-V
- Frecuencia: 360 MHz (configurable)
- Cache: 512 KB L2 Cache

**Memoria**:
- Flash: 16 MB (Quad SPI)
- PSRAM: 32 MB (Octal SPI, 200 MHz)
- SRAM: ~500 KB interna

**Periféricos Clave**:
- **MIPI-DSI**: 4-lane, hasta 1920x1080@60fps
- **SDIO**: Para ESP-Hosted (ESP32-C6)
- **UART**: 5 puertos (usamos 2)
- **I2S**: 3 puertos (1 usado para audio)
- **I2C**: 2 puertos (1 usado para touch)
- **GPIO**: 54 pines programables

**Características**:
- Coprocesador de imagen (ISP)
- Acelerador de gráficos 2D (PPA)
- DMA dedicado para peripherals
- Soporte XIP (Execute In Place) desde PSRAM

## Pantalla y Touch

### LCD 10.1" 1280x800

**Especificaciones**:
- **Tamaño**: 10.1 pulgadas
- **Resolución**: 1280 x 800 pixels
- **Interface**: MIPI-DSI (4 data lanes)
- **Driver IC**: EK79007
- **Backlight**: LED controlado por PWM
- **Conexión**: 32-pin FPC

**Pines de Control**:
| Señal | GPIO ESP32-P4 | Función |
|-------|---------------|---------|
| PWM | 26 | Control de brillo del backlight |
| LCD_RST | 27 | Reset del panel LCD |
| MIPI_DSI | Interface dedicado | 4 data lanes + clock |

**Configuración en Código**:
```c
// En main.c
bsp_display_cfg_t cfg = {
    .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,      // 100 líneas
    .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,  // true
    .flags = {
        .buff_dma = false,     // No compatible con SPIRAM
        .buff_spiram = true,   // Buffers en PSRAM
        .sw_rotate = true,     // Rotación software
    }
};
bsp_display_start_with_config(&cfg);
bsp_display_rotate(NULL, LV_DISP_ROT_270);  // Rotación 270°
```

**IMPORTANTE**:
- `buff_spiram = true` es requerido para operación con PSRAM
- `buff_dma = false` porque DMA no es compatible con SPIRAM en P4
- Los buffers usan aproximadamente 1.2 MB de PSRAM

### Touch Controller GT911

**Especificaciones**:
- **Interface**: I2C
- **Direcciones**: 0x5D / 0x14 (auto-detectado)
- **Resolución**: Hasta 10 puntos táctiles
- **Frecuencia I2C**: 400 kHz (Fast Mode)

**Pines I2C**:
| Señal | GPIO | Función |
|-------|------|---------|
| SDA | Por BSP | Datos I2C |
| SCL | Por BSP | Clock I2C |
| INT | Por BSP | Interrupción táctil |
| RST | Por BSP | Reset del GT911 |

**Inicialización**:
El controlador táctil se inicializa automáticamente por el BSP (Board Support Package) de ESP32-P4-Function-EV-Board.

**Calibración**:
La calibración se realiza automáticamente al detectar el panel. Los parámetros de calibración están embebidos en el firmware del GT911.

## Módulo ESP-Hosted

### ESP32-C6-MINI-1

El sistema utiliza ESP-Hosted para externalizar WiFi y BLE a un ESP32-C6 dedicado.

**Arquitectura ESP-Hosted**:
```
┌──────────────┐          ┌─────────────┐
│   ESP32-P4   │  SDIO    │  ESP32-C6   │
│   (Host)     │◄────────►│  (Slave)    │
│              │          │             │
│  NimBLE      │          │  WiFi Stack │
│  WiFi API    │          │  BLE Stack  │
└──────────────┘          └─────────────┘
```

**Interface SDIO**:
| Señal | Función |
|-------|---------|
| CLK | Clock SDIO |
| CMD | Command |
| D0-D3 | 4-bit data bus |

**Firmware del C6**:
- **Repositorio**: https://github.com/espressif/esp-hosted
- **Modo**: Slave (co-processor)
- **Versión**: Compatible con ESP-IDF v5.3

**Funciones Provistas**:
1. **WiFi**: STA mode, scan, connect, IP stack
2. **BLE**: GAP/GATT via NimBLE
3. **Comunicación**: Comandos serializados via SDIO

**Ventajas de ESP-Hosted**:
- Descargar procesamiento de RF del P4
- Firmware WiFi/BLE actualizable independientemente
- Aislamiento de bugs de radio

**Inicialización**:
```c
// En main.c
ret = esp_hosted_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_hosted_init() failed");
}
```

## Conexiones GPIO

### Tabla Completa de GPIO Utilizados

| GPIO | Función | Dirección | Notas |
|------|---------|-----------|-------|
| 4 | RS485 TX | Salida | UART1, comunicación con Base |
| 5 | RS485 RX | Entrada | UART1, comunicación con Base |
| 26 | LCD Backlight PWM | Salida | Control de brillo |
| 27 | LCD Reset | Salida | Reset del panel |
| - | MIPI-DSI | Interface | 4 lanes + clock (pines dedicados) |
| - | Touch I2C SDA | I/O | Por BSP |
| - | Touch I2C SCL | Salida | Por BSP |
| - | SDIO (ESP-Hosted) | Interface | CLK, CMD, D0-D3 |
| - | I2S Audio | Interface | BCLK, LRCLK, DOUT, MCLK |
| - | Botones (x10) | Entrada | GPIOs definidos por BSP (5 a cada lado) |

**NOTA**: Los pines de Touch I2C, SDIO, I2S y Botones son gestionados automáticamente por el BSP (Board Support Package) y no requieren configuración manual.

### Pines Disponibles

Después de asignar todos los periféricos, quedan disponibles aproximadamente 25 GPIOs para expansión futura.

## Sistema de Audio

### Codec ES8311

**Especificaciones**:
- **Interface**: I2S
- **Sample Rate**: Hasta 48 kHz
- **Bit Depth**: 16/24/32 bits
- **Canales**: Stereo
- **DAC**: 24-bit Sigma-Delta
- **Control**: I2C

**Configuración I2S**:
```c
// En audio.c
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
    .slot_cfg = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = GPIO_NUM_X,  // Definido por BSP
        .ws   = GPIO_NUM_Y,  // Definido por BSP
        .dout = GPIO_NUM_Z,  // Definido por BSP
        .din  = I2S_GPIO_UNUSED,
    },
};
```

**Uso**:
El sistema de audio reproduce sonidos de eventos (inicio, parada, alertas) almacenados como arrays de PCM en `audio.c`.

## Interfaz RS485

### Transceiver MAX485

**Conexiones**:
| MAX485 Pin | Función | ESP32-P4 GPIO |
|------------|---------|---------------|
| DI | Data Input | GPIO 4 (TX) |
| RO | Receiver Output | GPIO 5 (RX) |
| DE | Driver Enable | Automático (por hardware) |
| RE | Receiver Enable | Automático (por hardware) |
| A | RS485 A | Terminal screw |
| B | RS485 B | Terminal screw |

**Configuración UART**:
```c
// En cm_master.c
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};
uart_driver_install(UART_NUM_1, 512, 512, 0, NULL, 0);
uart_param_config(UART_NUM_1, &uart_config);
uart_set_pin(UART_NUM_1, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
```

**Control de Dirección**:
El MAX485 utilizado tiene control automático de dirección mediante detección de transmisión, por lo que no se requieren pines DE/RE en el ESP32-P4.

**Cableado RS485**:
```
Consola (MAX485)          Cable           Base (MAX485)
    A  ─────────────────── Wire 1 ─────────────  A
    B  ─────────────────── Wire 2 ─────────────  B
   GND ─────────────────── Wire 3 ─────────────  GND
```

**Longitud Máxima**:
- Hasta 1200 metros @ 115200 baud (cable de par trenzado)
- Para instalaciones más cortas (<100m), cable estándar es suficiente

## Botones Físicos

### Layout de Botones

El sistema tiene 10 botones físicos organizados en el panel frontal (5 a cada lado):

**Lado Izquierdo (5 botones)**:
- `SPEED +`: Incrementar velocidad
- `SPEED -`: Decrementar velocidad
- `CLIMB +`: Incrementar inclinación
- `CLIMB -`: Decrementar inclinación
- `STOP`: Parar/Reanudar

**Lado Derecho (5 botones)**:
- `SET SPEED`: Entrada manual de velocidad
- `SET CLIMB`: Entrada manual de inclinación
- `COOL DOWN`: Activar enfriamiento
- `HEAD`: Toggle ventilador cabeza
- `CHEST`: Toggle ventilador pecho

### Configuración de Botones

Los botones son gestionados por el BSP y detectados mediante polling en `button_handler.c`:

```c
// En button_handler.c
typedef struct {
    uint32_t gpio_mask;
    void (*callback)(void);
    const char *name;
} button_config_t;

static const button_config_t buttons[] = {
    { BSP_BUTTON_SPEED_UP_MASK,   ui_speed_inc, "SPEED+" },
    { BSP_BUTTON_SPEED_DOWN_MASK, ui_speed_dec, "SPEED-" },
    // ... más botones
};
```

**Anti-Rebote**:
- Debounce por software: 50 ms
- Detección de flanco descendente (press)
- No se detecta release para simplificar

## Alimentación

### Fuente de Poder

**Entrada**:
- Voltaje: 12V DC (típico) o 5V USB-C
- Corriente mínima recomendada: 3A
- Conector: Barrel jack 2.1mm o USB-C

**Reguladores Onboard**:
- 3.3V @ 1A (digital)
- 1.8V @ 500mA (IO)
- Reguladores para PSRAM, Flash

**Consumo de Corriente Estimado**:
| Componente | Corriente (típica) | Corriente (pico) |
|------------|-------------------|------------------|
| ESP32-P4 Core | 200 mA | 400 mA |
| PSRAM | 100 mA | 150 mA |
| LCD Backlight | 400 mA | 500 mA |
| ESP32-C6 (WiFi TX) | 200 mA | 350 mA |
| Audio + Misc | 100 mA | 200 mA |
| **Total** | **1.0 A** | **1.6 A** |

**Recomendación**: Fuente de 5V @ 3A es adecuada para operación normal con margen.

## Esquemático de Conexiones

### Diagrama de Bloques

```
                     ┌────────────────────────────────┐
                     │                                │
                     │        ESP32-P4-WROOM-1        │
                     │                                │
                     │  ┌──────────────────────────┐  │
                     │  │   Dual-Core RISC-V       │  │
                     │  │   @ 360 MHz              │  │
                     │  └──────────────────────────┘  │
                     │                                │
┌────────────┐       │  ┌──────────────────────────┐  │
│ LCD 10.1"  │◄──────┼──┤   MIPI-DSI (4-lane)     │  │
│ 1280x800   │       │  └──────────────────────────┘  │
│ EK79007    │       │                                │
│            │       │  ┌──────────────────────────┐  │
│ Touch GT911│◄──────┼──┤   I2C (400kHz)           │  │
└────────────┘       │  └──────────────────────────┘  │
                     │                                │
┌────────────┐       │  ┌──────────────────────────┐  │
│ ESP32-C6   │◄──────┼──┤   SDIO (4-bit)           │  │
│ (ESP-Hosted│       │  └──────────────────────────┘  │
│  WiFi/BLE) │       │                                │
└────────────┘       │  ┌──────────────────────────┐  │
                     │  │   UART1 (RS485)          │──┼──► MAX485
┌────────────┐       │  └──────────────────────────┘  │     ↓
│ ES8311     │◄──────┼──┤   I2S                    │  │   Base Module
│ Audio Codec│       │  └──────────────────────────┘  │
└────────────┘       │                                │
                     │  ┌──────────────────────────┐  │
                     │  │   GPIO x10 (Botones)     │◄─┼── Botones (5 por lado)
                     │  └──────────────────────────┘  │
                     │                                │
                     └────────────────────────────────┘
```

## Consideraciones de Diseño

### PSRAM y XIP

El ESP32-P4 utiliza PSRAM con XIP (Execute In Place), lo que permite ejecutar código directamente desde PSRAM. Esto introduce consideraciones especiales:

**Problema conocido con Cache Sync**:
- Operaciones intensivas (TLS, crypto) generan muchos cache sync
- Esto puede causar artefactos visuales temporales en el display
- **Solución**: Buffers en SPIRAM sin DMA

**Configuración Óptima**:
```c
cfg.flags.buff_dma = false;      // Evitar DMA con SPIRAM
cfg.flags.buff_spiram = true;    // Usar PSRAM para buffers
```

### Thermal Management

El ESP32-P4 @ 360 MHz puede generar calor significativo:

**Recomendaciones**:
- Disipador de calor opcional en el módulo WROOM-1
- Ventilación adecuada en el gabinete
- Considerar reducir frecuencia a 240 MHz si hay problemas térmicos

**Monitoreo de Temperatura**:
```c
// Leer sensor de temperatura interno
float temp = esp_chip_info_get_cpu_freq_mhz();  // Indirecto
```

### EMI/EMC

Para minimizar interferencias electromagnéticas:

1. **Cable RS485**: Usar par trenzado apantallado
2. **Fuente de Alimentación**: Filtros LC en entrada
3. **Layout PCB**: Ground plane continuo, vias de stitching
4. **Cables**: Mantener cables de señal cortos y alejados de fuentes de ruido

## Expansión Futura

### GPIOs Disponibles

Aproximadamente 25 GPIOs libres para:
- Sensores adicionales (temperatura, humedad)
- Relés de control
- LEDs indicadores
- Interface SPI para almacenamiento externo

### Interfaces Disponibles

- **SPI**: 1 interface completo disponible
- **I2C**: 1 puerto adicional
- **UART**: 3 puertos adicionales
- **CAN**: 2 controladores (TWAI)
- **USB**: 1 puerto OTG

## Referencias

- **ESP32-P4 Datasheet**: https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf
- **ESP32-P4-Function-EV-Board**: https://docs.espressif.com/projects/esp-dev-kits/
- **EK79007 LCD Driver**: https://dl.espressif.com/dl/schematics/display_driver_chip_EK79007AD_datasheet.pdf
- **GT911 Touch Controller**: Datasheet del fabricante
- **ESP-Hosted**: https://github.com/espressif/esp-hosted

---

**Última actualización**: 2025-11-05
