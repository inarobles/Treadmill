# Solución a Problemas de Display durante Descarga HTTPS

## Problema Identificado

Durante las descargas HTTPS, los errores de cache sync (`esp_cache_msync invalid addr`) causan:
- ❌ Zonas negras temporales en la pantalla
- ❌ Corrupción visual del framebuffer de LVGL
- ❌ Interferencia con el rendering del display

**Causa**: ESP32-P4 con PSRAM + XIP no tiene cache coherent. Las operaciones intensivas de TLS/crypto durante HTTPS generan cientos de operaciones de cache sync que interfieren con el acceso al framebuffer.

## Solución Aplicada

### 1. Descarga Manual (No Automática)

La descarga **NO se inicia automáticamente** al conectarse WiFi. Esto evita que interfiera con el inicio de la UI.

### 2. Función Manual para Descargar

```c
#include "wifi_client.h"

// Llamar cuando el display esté estable
void on_button_download_press(void) {
    wifi_download_file();  // Inicia la descarga
}
```

### 3. Acceso al Contenido Descargado

Después de la descarga (esperar 3-4 segundos):

```c
#include "wifi_client.h"

if (g_downloaded_file_content != NULL) {
    // El archivo está listo
    printf("Downloaded: %s\n", g_downloaded_file_content);
    printf("Size: %d bytes\n", g_downloaded_file_size);
}
```

## Cuándo Llamar a wifi_download_file()

### ✅ BUENOS Momentos:
- Cuando el usuario presiona un botón "Descargar"
- En una pantalla de configuración/settings
- Durante una pantalla de "splash" o "loading"
- Cuando la pantalla muestra contenido estático

### ❌ MALOS Momentos:
- Durante animaciones LVGL
- Mientras se renderiza la UI principal
- En el arranque de la aplicación
- Durante transiciones de pantalla

## Ejemplo de Uso Recomendado

```c
// En tu código de UI (ui.c o similar)
#include "wifi_client.h"

static void download_button_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        // Mostrar mensaje al usuario
        lv_label_set_text(status_label, "Descargando...\nPueden aparecer artefactos visuales temporales");

        // Iniciar descarga
        wifi_download_file();

        // Esperar y mostrar resultado (en un timer o callback)
        // Ver ejemplo de delayed_print_task en wifi_client.c
    }
}
```

## Monitoreo del Progreso

El log mostrará:
```
I (XXXXX) WIFI_DOWNLOADER: Starting manual file download...
I (XXXXX) WIFI_DOWNLOADER: HTTP_EVENT_ON_CONNECTED
I (XXXXX) WIFI_DOWNLOADER: Download complete. Total received: 344 bytes
I (XXXXX) WIFI_DOWNLOADER: File downloaded successfully
I (XXXXX) WIFI_DOWNLOADER: Waiting 3 seconds for system to stabilize...
I (XXXXX) WIFI_DOWNLOADER: === BEGIN DOWNLOADED FILE CONTENT ===
Prueba exitosisísima! :-)
[...]
I (XXXXX) WIFI_DOWNLOADER: === END DOWNLOADED FILE CONTENT (344 bytes) ===
```

## Notas Importantes

1. **Los errores de cache son inevitables** - Es una limitación del hardware ESP32-P4 con PSRAM+XIP
2. **El display se recupera automáticamente** - Las zonas negras desaparecen al terminar la descarga
3. **La funcionalidad no se ve afectada** - Solo es un problema visual temporal
4. **No ejecutar descargas frecuentes** - Hazlo solo cuando sea necesario

## Alternativas Futuras

Si necesitas descargas frecuentes sin afectar el display:
- Usar ESP32-P4 sin XIP (requiere aplicación más pequeña)
- Actualizar a futuras versiones de ESP-IDF con mejoras en cache coherency
- Usar un co-procesador separado para networking (ESP32-C6 vía ESP-Hosted ya lo estás usando para WiFi, podrías hacer HTTP desde ahí)
