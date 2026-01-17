# Guía de Actualización del Firmware ESP32-C6 (Co-procesador)

## Descripción General

Este documento describe el proceso completo para actualizar el firmware del ESP32-C6 que actúa como co-procesador WiFi/BLE en el sistema SmartRowing. El ESP32-C6 se comunica con el ESP32-P4 (host) a través de SDIO usando el componente ESP-Hosted.

## Arquitectura del Sistema

```
┌─────────────────┐         SDIO          ┌─────────────────┐
│   ESP32-P4      │◄─────────────────────►│   ESP32-C6      │
│   (Host)        │                        │   (Slave)       │
│                 │                        │                 │
│ - App Principal │                        │ - WiFi Stack    │
│ - LVGL UI       │                        │ - BLE Stack     │
│ - ESP-Hosted    │                        │ - ESP-Hosted    │
│   v2.8.3        │                        │   Firmware      │
└─────────────────┘                        └─────────────────┘
```

## Versiones Actuales

- **ESP-IDF**: v5.5.1
- **ESP-Hosted (Host)**: v2.8.3
- **ESP-Hosted (Slave)**: v2.8.3
- **Firmware C6**: `network_adapter_esp32c6.bin`

## Ubicación del Firmware

El firmware del ESP32-C6 se almacena en:
- **Partición**: `slave_fw` (offset 0xd10000, tamaño 2MB)
- **Archivo binario**: `build/network_adapter_esp32c6.bin`
- **Generado por**: Componente `espressif__esp_hosted` durante el build del host

## Proceso de Actualización

### 1. Verificación de Versión Actual

Antes de actualizar, verifica la versión actual del firmware del C6:

```c
#include "esp_hosted.h"

esp_hosted_coprocessor_fwver_t ver_info;
esp_err_t ret = esp_hosted_get_coprocessor_fwversion(&ver_info);

if (ret == ESP_OK) {
    ESP_LOGI(TAG, "C6 Firmware: v%d.%d.%d", 
             ver_info.major, ver_info.minor, ver_info.patch);
}
```

### 2. Actualización del Componente ESP-Hosted

#### 2.1. Modificar `main/idf_component.yml`

```yaml
dependencies:
  espressif/esp_hosted: "2.8.3"  # Especificar versión exacta
  lvgl/lvgl: "^8.3"
```

#### 2.2. Ejecutar actualización de componentes

```bash
cd c:\Esp\SmartRowing
idf.py reconfigure
```

Esto descargará ESP-Hosted v2.8.3 en `managed_components/espressif__esp_hosted/`.

### 3. Parches Necesarios para v2.8.3

#### 3.1. Detección de Slave Target (ESP32-C6)

**Archivo**: `managed_components/espressif__esp_hosted/host/port/esp/freertos/include/port_esp_hosted_host_config.h`

**Problema**: El sistema no detecta automáticamente ESP32-C6 como slave cuando el host es ESP32-P4.

**Solución**: Modificar la línea ~69:

```c
// ANTES:
#elif CONFIG_SLAVE_IDF_TARGET_ESP32C6
  #define H_SLAVE_TARGET_ESP32C6 1

// DESPUÉS:
#elif (CONFIG_SLAVE_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32P4)
  #define H_SLAVE_TARGET_ESP32C6 1
```

#### 3.2. Dummy Macros SPI para Dependencias Estructurales

**Archivo**: `managed_components/espressif__esp_hosted/host/port/esp/freertos/include/port_esp_hosted_host_config.h`

**Problema**: v2.8.3 tiene dependencias estructurales que referencian macros SPI incluso cuando solo SDIO está habilitado.

**Solución**: Agregar después de la línea ~84:

```c
/* Forced dummy defines for SPI to satisfy v2.8.3 structural dependencies when SDIO is active */
#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE)
  #ifndef CONFIG_ESP_HOSTED_SPI_GPIO_HANDSHAKE
    #define CONFIG_ESP_HOSTED_SPI_GPIO_HANDSHAKE -1
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_GPIO_DATA_READY
    #define CONFIG_ESP_HOSTED_SPI_GPIO_DATA_READY -1
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_GPIO_MOSI
    #define CONFIG_ESP_HOSTED_SPI_GPIO_MOSI -1
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_GPIO_MISO
    #define CONFIG_ESP_HOSTED_SPI_GPIO_MISO -1
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_GPIO_CLK
    #define CONFIG_ESP_HOSTED_SPI_GPIO_CLK -1
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_GPIO_CS
    #define CONFIG_ESP_HOSTED_SPI_GPIO_CS -1
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_TX_Q_SIZE
    #define CONFIG_ESP_HOSTED_SPI_TX_Q_SIZE 0
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_RX_Q_SIZE
    #define CONFIG_ESP_HOSTED_SPI_RX_Q_SIZE 0
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_MODE
    #define CONFIG_ESP_HOSTED_SPI_MODE 0
  #endif
  #ifndef CONFIG_ESP_HOSTED_SPI_CLK_FREQ
    #define CONFIG_ESP_HOSTED_SPI_CLK_FREQ 0
  #endif
#endif

#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE)
  #define H_TRANSPORT_IN_USE H_TRANSPORT_SDIO
  /* Define H_SPI_* macros as dummies to satisfy v2.8.3 transport_defaults.c */
  #define H_HANDSHAKE_ACTIVE_HIGH 0
  #define H_DATAREADY_ACTIVE_HIGH 0
  #define H_HS_VAL_ACTIVE 0
  #define H_HS_VAL_INACTIVE 0
  #define H_HS_INTR_EDGE 0
  #define H_DR_VAL_ACTIVE 0
  #define H_DR_VAL_INACTIVE 0
  #define H_DR_INTR_EDGE 0
  #define H_GPIO_HANDSHAKE_Port NULL
  #define H_GPIO_HANDSHAKE_Pin -1
  #define H_GPIO_DATA_READY_Port NULL
  #define H_GPIO_DATA_READY_Pin -1
  #define H_GPIO_MOSI_Port NULL
  #define H_GPIO_MOSI_Pin -1
  #define H_GPIO_MISO_Port NULL
  #define H_GPIO_MISO_Pin -1
  #define H_GPIO_SCLK_Port NULL
  #define H_GPIO_SCLK_Pin -1
  #define H_GPIO_CS_Port NULL
  #define H_GPIO_CS_Pin -1
  #define H_SPI_TX_Q 0
  #define H_SPI_RX_Q 0
  #define H_SPI_MODE 0
  #define H_SPI_FD_CLK_MHZ 0
#endif
```

#### 3.3. Aislamiento del Bloque SPI

**Archivo**: `managed_components/espressif__esp_hosted/host/port/esp/freertos/include/port_esp_hosted_host_config.h`

**Problema**: El bloque de configuración SPI se compila incluso cuando SDIO está activo.

**Solución**: Cambiar la línea ~88 de:

```c
// ANTES:
#ifdef CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE

// DESPUÉS:
#if !defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE) && defined(CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE)
```

#### 3.4. Macros WiFi Remote sin Redefiniciones

**Archivo**: `managed_components/espressif__esp_wifi_remote/idf_tag_v5.5.1/include/injected/esp_wifi.h`

**Problema**: Warnings de redefinición de `CONFIG_WIFI_RMT_TX_BUFFER_TYPE` y otras macros.

**Solución**: Las macros ya están definidas con guardas `#ifndef` en las líneas 50-75. Verificar que no haya duplicados.

### 4. Configuración SDIO

#### 4.1. Verificar `sdkconfig.defaults`

Asegurar que contenga:

```ini
# ESP-Hosted Configuration for ESP32-P4 Function EV Board
CONFIG_ESP_WIFI_REMOTE_ENABLED=y
CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED=y
CONFIG_SLAVE_IDF_TARGET_ESP32C6=y
CONFIG_IDF_SLAVE_TARGET="esp32c6"
CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET="esp32c6"
CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM=7
```

#### 4.2. Configurar en menuconfig (si es necesario)

```bash
idf.py menuconfig
```

Navegar a:
- `Component config` → `ESP-Hosted` → `Transport Layer` → Seleccionar `SDIO`

### 5. Compilación

```bash
cd c:\Esp\SmartRowing
idf.py build
```

**Verificaciones durante el build:**
- ✅ No debe aparecer "Unknown Slave Target"
- ✅ No debe haber errores de "undeclared identifier" para macros SPI
- ✅ Warnings de deprecación en componentes externos son normales y benignos

**Salida esperada:**
```
[1654/1654] Generating binary image from built executable
esptool.py v4.8.1
Creating esp32p4 image...
Merged 2 ELF sections
Successfully created esp32p4 image.
Generated C:/Esp/SmartRowing/build/SmartRowingApp.bin
```

### 6. Flasheo del Firmware

#### 6.1. Flashear el Host (ESP32-P4)

```bash
idf.py -p COM10 flash
```

Esto flasheará:
- Bootloader
- Partition table
- App principal (SmartRowingApp.bin)
- **Firmware del C6** (network_adapter_esp32c6.bin en partición slave_fw)

#### 6.2. Verificación en Monitor Serie

```bash
idf.py -p COM10 monitor
```

**Salida esperada:**
```
I (xxxx) esp_hosted: Initialising ESP-Hosted
I (xxxx) esp_hosted: ESP-Hosted: Host [2.8.3] Slave [2.8.3]
I (xxxx) esp_hosted: SDIO Host interface initialized
I (xxxx) esp_hosted: Coprocessor firmware version: 2.8.3
```

**NO debe aparecer:**
```
W (xxxx) esp_hosted: Version mismatch: Host [2.8.3] < Co-proc [X.X.X]
```

### 7. OTA del Firmware C6 (Opcional)

Si necesitas actualizar solo el firmware del C6 sin flashear todo el sistema:

```c
#include "esp_hosted_ota.h"

// Ruta al nuevo firmware
const char *fw_path = "/spiffs/network_adapter_esp32c6.bin";

// Iniciar OTA
esp_err_t ret = esp_hosted_ota(fw_path);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "C6 firmware updated successfully");
    // Reiniciar para aplicar
    esp_restart();
} else {
    ESP_LOGE(TAG, "C6 firmware update failed: %s", esp_err_to_name(ret));
}
```

## Troubleshooting

### Problema: "Unknown Slave Target"

**Causa**: El sistema no detecta ESP32-C6 como slave cuando el host es ESP32-P4.

**Solución**: Aplicar parche 3.1 (Detección de Slave Target).

### Problema: "undeclared identifier CONFIG_ESP_HOSTED_SPI_*"

**Causa**: v2.8.3 tiene dependencias estructurales que referencian macros SPI.

**Solución**: Aplicar parches 3.2 y 3.3 (Dummy Macros SPI y Aislamiento).

### Problema: Warnings de redefinición de CONFIG_WIFI_RMT_*

**Causa**: Macros definidas sin guardas `#ifndef`.

**Solución**: Aplicar parche 3.4 (Macros WiFi Remote).

### Problema: sdkconfig se regenera con SPI en lugar de SDIO

**Causa**: Kconfig no selecciona SDIO automáticamente.

**Solución**: 
1. Ejecutar `idf.py menuconfig`
2. Configurar manualmente SDIO
3. Guardar y compilar sin `reconfigure`

### Problema: Version mismatch después de actualizar

**Causa**: El firmware del C6 no se actualizó correctamente.

**Solución**:
1. Verificar que `network_adapter_esp32c6.bin` esté en `build/`
2. Flashear completamente con `idf.py -p COM10 flash`
3. Verificar en monitor serie la versión del co-procesador

## Checklist de Actualización

- [ ] Actualizar `idf_component.yml` con versión 2.8.3
- [ ] Ejecutar `idf.py reconfigure`
- [ ] Aplicar parche 3.1: Detección de Slave Target
- [ ] Aplicar parche 3.2: Dummy Macros SPI
- [ ] Aplicar parche 3.3: Aislamiento del Bloque SPI
- [ ] Verificar parche 3.4: Macros WiFi Remote (ya aplicado)
- [ ] Verificar `sdkconfig.defaults`
- [ ] Configurar SDIO en menuconfig (si es necesario)
- [ ] Compilar con `idf.py build`
- [ ] Verificar que no hay errores críticos
- [ ] Flashear con `idf.py -p COM10 flash`
- [ ] Verificar en monitor serie que no hay version mismatch
- [ ] Verificar comunicación SDIO funcional

## Referencias

- [ESP-Hosted GitHub](https://github.com/espressif/esp-hosted)
- [ESP-Hosted Documentation](https://docs.espressif.com/projects/esp-hosted/)
- [ESP32-P4 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-p4_technical_reference_manual_en.pdf)
- [ESP32-C6 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)

## Notas Importantes

1. **Compatibilidad de Versiones**: Es crítico que el host y el slave tengan la misma versión de ESP-Hosted (2.8.3).

2. **Parches Temporales**: Los parches aplicados a `managed_components` son específicos para v2.8.3 y pueden necesitar reaplicación si se actualiza ESP-Hosted en el futuro.

3. **SDIO vs SPI**: Este sistema usa SDIO exclusivamente. No mezclar configuraciones SPI/SDIO.

4. **Warnings Benignos**: Los warnings de componentes externos (`esp_lvgl_port`, `esp-audio-player`) son normales y no afectan la funcionalidad.

5. **Backup**: Antes de actualizar, hacer backup del firmware actual del C6 si es posible.

---

**Última actualización**: 2025-12-28
**Versión del documento**: 1.0
**Autor**: Sistema de actualización automática SmartRowing
