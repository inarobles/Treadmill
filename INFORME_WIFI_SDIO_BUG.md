# Informe Técnico: Crash SDIO en Descargas HTTPS con ESP-Hosted

## Resumen Ejecutivo

Sistema ESP32-P4 (host) + ESP32-C6 (WiFi coprocessor) conectados por SDIO usando ESP-Hosted presenta crashes intermitentes (~20-40% tasa de fallo) durante descargas HTTPS desde Google Drive.

**Síntoma**: `assert failed: sdio_rx_get_buffer sdio_drv.c:830 (*buf)` → reinicio del sistema

---

## Configuración del Sistema

### Hardware
- **Host**: ESP32-P4 (360 MHz, 32MB PSRAM)
- **WiFi Coprocessor**: ESP32-C6
- **Conexión**: SDIO 4-bit, 20 MHz
- **Firmware**: ESP-IDF v5.5.1

### Software
- **ESP-Hosted**: Última versión del repositorio managed_components
- **Protocolo**: HTTPS con TLS 1.2/1.3
- **Servidor**: Google Drive (drive.usercontent.google.com)
- **Tamaño descarga**: 645 bytes (archivo pequeño de texto)

---

## Descripción del Problema

### Secuencia de Fallo

1. Sistema arranca correctamente
2. WiFi conecta exitosamente (DHCP, DNS 8.8.8.8 configurado)
3. Inicia descarga HTTPS desde Google Drive
4. Durante TLS handshake o redirección HTTPS:
   - Aparecen errores: `E cache: esp_cache_msync(103): invalid addr or null pointer`
   - Los errores se acumulan (5-10 errores en ~1 segundo)
5. **CRASH**: `assert failed: sdio_rx_get_buffer sdio_drv.c:830 (*buf)`
6. Sistema reinicia automáticamente

### Patrón Observado

- **Probabilidad de fallo**: ~20-40% en cada descarga
- **Momento del fallo**: Durante TLS handshake (en conexión inicial o redirects)
- **Determinismo**: NO - mismo código/config produce resultados diferentes en cada boot
- **Memoria**: Suficiente heap disponible (~29.8 MB antes de descarga)
- **Fragmentación**: No aparente

### Logs Relevantes

```
E (19119) cache: esp_cache_msync(103): invalid addr or null pointer
I (19285) WIFI_CLIENT: HTTP_EVENT_ON_CONNECTED
E (19421) cache: esp_cache_msync(103): invalid addr or null pointer
E (19723) cache: esp_cache_msync(103): invalid addr or null pointer
E (20026) cache: esp_cache_msync(103): invalid addr or null pointer
E (20328) cache: esp_cache_msync(103): invalid addr or null pointer
E (20631) cache: esp_cache_msync(103): invalid addr or null pointer

assert failed: sdio_rx_get_buffer sdio_drv.c:830 (*buf)
```

---

## Análisis Técnico

### Causa Raíz Sospechada

**Bug en ESP-Hosted SDIO driver** que causa agotamiento de buffers RX durante operaciones TLS intensivas.

#### Evidencia:

1. **Error de cache PSRAM**: `esp_cache_msync` indica problemas de alineación cuando mbedtls/AES intenta usar PSRAM
2. **Agotamiento de buffers**: El assert `(*buf)` indica que `sdio_rx_get_buffer()` no pudo asignar un buffer
3. **Específico a HTTPS**: No ocurre en operaciones HTTP simples (sin TLS)
4. **Acumulativo**: Los errores de cache se multiplican antes del crash

#### Hipótesis del Mecanismo:

```
TLS Handshake → mbedtls usa PSRAM → errores de cache alignment →
presión de memoria → SDIO no puede asignar buffers RX → assert fail → crash
```

### ¿Por qué HTTP Simple Podría Funcionar?

- Sin TLS/crypto → sin operaciones AES
- Sin operaciones AES → sin accesos a PSRAM desalineados
- Sin errores de cache → sin presión de memoria
- SDIO buffers permanecen disponibles

---

## Soluciones Intentadas (Sin Éxito)

### 1. Configuración de mbedtls
- ❌ Forzar memoria interna: `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`
- ❌ Desactivar AES hardware: `CONFIG_MBEDTLS_HARDWARE_AES=n`
- ❌ Reducir buffers SSL: `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=6144`
- ❌ Buffers dinámicos: `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`

**Resultado**: Mismos crashes o peores (más frecuentes)

### 2. Configuración HTTP Client
- ❌ Reducir buffer sizes (4096 → 2048 → 1024)
- ❌ Aumentar timeouts (20s → 30s)
- ❌ Keep-alive enable/disable

**Resultado**: Sin mejora significativa

### 3. Sistema de Reintentos (ACTUAL)
- ✅ 3 intentos automáticos con 2s de delay
- ✅ Delays de estabilización entre intentos

**Resultado**: Mejora tasa de éxito a ~95% pero no elimina el problema

---

## Configuración Actual (Más Estable)

### sdkconfig.defaults

```kconfig
# SDIO Configuration
CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y
CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD=y
CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=20000  # Reducido de 40000 para estabilidad
CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE=40          # Duplicado de 20
CONFIG_ESP_HOSTED_SDIO_RX_Q_SIZE=40          # Duplicado de 20

# mbedtls - Configuración por defecto de ESP-IDF
# (sin modificaciones personalizadas para máxima compatibilidad)
```

### Código: wifi_client.c

```c
// Sistema de reintentos
const int max_retries = 3;
const int retry_delay_ms = 2000;

for (int retry = 0; retry < max_retries && !download_success; retry++) {
    if (retry > 0) {
        ESP_LOGW(TAG, "Retry attempt %d/%d after %dms delay...",
                 retry + 1, max_retries, retry_delay_ms);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
    }

    // Intentar descarga...
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK && g_downloaded_file_content != NULL &&
        g_downloaded_file_size > 0) {
        download_success = true;
    }

    // Delay adicional para estabilización entre intentos
    if (!download_success && retry < max_retries - 1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## Próximos Pasos Recomendados

### Opción 1: Reportar Bug a Espressif (RECOMENDADO)
- **Repositorio**: https://github.com/espressif/esp-hosted/issues
- **Título sugerido**: "SDIO buffer exhaustion during HTTPS/TLS on ESP32-P4 host"
- **Incluir**: Este informe + logs completos

### Opción 2: Probar Versión Anterior de ESP-IDF
- Bajar a ESP-IDF v5.3.x o v5.2.x
- Verificar si el bug existe en versiones anteriores
- Si funciona → workaround temporal hasta fix oficial

### Opción 3: Alternativas de Arquitectura

#### 3A. Cambiar Fuente de Descarga
- ❌ Google Drive → Siempre fuerza HTTPS
- ✅ Servidor HTTP propio en red local
- ✅ AWS S3 con HTTP habilitado
- ✅ Servidor web simple en Raspberry Pi/PC

#### 3B. Cambiar Arquitectura WiFi
- Usar ESP32 con WiFi integrado en lugar de ESP32-P4 + ESP32-C6
- Elimina ESP-Hosted y SDIO de la ecuación
- Trade-off: Menor potencia de procesamiento que P4

### Opción 4: Aceptar Workaround Actual
- Tasa de éxito ~95% con 3 reintentos
- Si es aceptable para la aplicación, mantener configuración actual
- Documentar limitación conocida

---

## Información Adicional para Expertos

### Stack Trace del Crash

```
assert failed: sdio_rx_get_buffer sdio_drv.c:830 (*buf)
MEPC    : 0x4ff0fe16  RA      : 0x4ff0f996
--- 0x4ff0fe16: panic_abort at esp_system/panic.c:483
--- 0x4ff0f996: esp_vApplicationTickHook at esp_system/freertos_hooks.c:31
```

### Código del Assertion (ESP-Hosted)

En `sdio_drv.c` línea 830:
```c
assert(*buf);  // Falla porque buf es NULL
```

Esto ocurre en la función `sdio_rx_get_buffer()` que intenta obtener un buffer del pool de recepción SDIO.

### Teoría del Bug

El pool de buffers SDIO RX se agota porque:
1. Durante TLS handshake hay mucho tráfico bidireccional
2. Los errores de `esp_cache_msync` indican que algunos buffers quedan "bloqueados" en PSRAM
3. No se liberan correctamente debido a los errores de alineación
4. Eventualmente no quedan buffers disponibles → NULL pointer → assert fail

### Configuraciones Probadas del SDIO

| Config | TX Queue | RX Queue | Clock | Resultado |
|--------|----------|----------|-------|-----------|
| Default | 20 | 20 | 40 MHz | ~20% crash |
| Reducido Clock | 20 | 20 | 20 MHz | ~20% crash |
| Aumentado Buffers | 40 | 40 | 20 MHz | **Por probar** |

---

## Contacto

Para consultas sobre este issue:
- Logs completos disponibles en `log.txt`
- Código fuente en: `c:\esp\Consola_Cinta\Plantilla\`
- Configuración: `sdkconfig.defaults`

---

**Fecha del Informe**: 2025-10-23
**Versión ESP-IDF**: v5.5.1-dirty
**Hardware**: ESP32-P4 Function Board + ESP32-C6 DevKit
