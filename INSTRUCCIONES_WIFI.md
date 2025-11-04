# Instrucciones para completar la integración del selector WiFi

El sistema de selección de red WiFi ha sido implementado con los siguientes archivos ya creados:

## Archivos completados:
1. ✅ **wifi_manager.h** - Header con funciones de escaneo y gestión de credenciales
2. ✅ **wifi_manager.c** - Implementación del gestor WiFi
3. ✅ **wifi_client.h** - Agregada función `wifi_client_connect()`
4. ✅ **wifi_client.c** - Modificado para soportar conexión dinámica
5. ✅ **ui.h** - Agregadas funciones públicas para WiFi
6. ✅ **ui.c** - Botón "1" cambiado a "WIFI" con callback `wifi_selector_event_cb`
7. ✅ **ui_wifi.c** - Archivo con todas las funciones WiFi UI

## Pasos pendientes para completar la integración:

### 1. Modificar `ui.c`

Agregar al inicio del archivo, después de los includes existentes:
```c
// Declaración de función externa para crear pantallas WiFi
extern void create_wifi_screens(void);
```

Modificar la función `ui_init()` (línea 1336) para agregar:
```c
void ui_init(void) {
    create_styles();
    create_training_select_screen();
    create_loading_screen();
    create_uploading_screen();
    create_main_screen();
    create_set_screen();
    create_shutdown_screen();
    create_wifi_screens();  // <-- AGREGAR ESTA LÍNEA
    lv_scr_load(scr_training_select);
}
```

### 2. Modificar `main/CMakeLists.txt`

Agregar `ui_wifi.c` a la lista de archivos fuente. El archivo debería verse así:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "ui.c"
        "ui_wifi.c"          # <-- AGREGAR ESTA LÍNEA
        "audio.c"
        "treadmill_state.c"
        "button_handler.c"
        "ble_client.c"
        "wifi_client.c"
        "wifi_manager.c"      # <-- AGREGAR ESTA LÍNEA TAMBIÉN
    INCLUDE_DIRS "."
    EMBED_FILES ${assets}
)
```

### 3. Modificar `main.c`

Agregar include al inicio:
```c
#include "wifi_manager.h"
```

En la función `app_main()`, después de `esp_hosted_init()` y antes de `g_state_mutex = xSemaphoreCreateMutex()`, agregar:
```c
// Initialize WiFi Manager
ret = wifi_manager_init();
if (ret != ESP_OK) {
    ESP_LOGW(TAG, "wifi_manager_init() failed with error: %d (continuando...)", ret);
}
```

### 4. Compilar y probar

```bash
cd c:/esp/Consola_Cinta/Plantilla
idf.py reconfigure
idf.py build
```

## Flujo de usuario final:

1. Usuario pulsa botón "WIFI" (antes "1") en pantalla de selección
2. Se escanean redes WiFi disponibles (pantalla "Escaneando...")
3. Se muestra lista de redes con señal
4. Usuario selecciona una red:
   - Si tiene contraseña guardada → Conecta automáticamente
   - Si no tiene contraseña → Muestra teclado numérico para introducirla
5. Al introducir 8+ dígitos, guarda credenciales y conecta
6. Vuelve a pantalla de selección de entrenamiento

## Notas importantes:

- Las contraseñas se guardan en NVS de forma persistente
- El teclado solo acepta números (0-9)
- Para contraseñas alfanuméricas, considera agregar más botones o usar un teclado QWERTY táctil
- El botón "WIFI" está siempre visible en la pantalla de selección de entrenamientos
