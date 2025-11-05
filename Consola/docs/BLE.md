# Cliente BLE - Monitores de Frecuencia Cardíaca

Documentación del cliente BLE para conexión con monitores de frecuencia cardíaca via ESP-Hosted.

## Índice

- [Descripción General](#descripción-general)
- [Arquitectura BLE + ESP-Hosted](#arquitectura-ble--esp-hosted)
- [Servicio Heart Rate](#servicio-heart-rate)
- [Flujo de Operación](#flujo-de-operación)
- [API Pública](#api-pública)
- [Persistencia NVS](#persistencia-nvs)
- [Reconexión Automática](#reconexión-automática)
- [Ejemplos de Uso](#ejemplos-de-uso)
- [Troubleshooting](#troubleshooting)

## Descripción General

El cliente BLE implementa conectividad con monitores de frecuencia cardíaca Bluetooth Low Energy, utilizando el stack NimBLE sobre ESP-Hosted.

**Características**:
- Escaneo de dispositivos Heart Rate
- Conexión y emparejamiento automático
- Notificaciones de BPM en tiempo real
- Persistencia de dispositivo guardado en NVS
- Reconexión automática al dispositivo guardado

**Stack**:
- **NimBLE**: Stack BLE ligero de Apache Mynewt
- **ESP-Hosted**: Transport layer (SDIO al ESP32-C6)
- **Servicio**: Heart Rate Service (UUID 0x180D)

## Arquitectura BLE + ESP-Hosted

### Diagrama

```
┌────────────────────────────────────────────┐
│         ESP32-P4 (Host)                    │
│                                            │
│  ┌──────────────────────────────────┐     │
│  │  Application (treadmill_state)   │     │
│  │  g_treadmill_state.real_pulse    │     │
│  └───────────────┬──────────────────┘     │
│                  │                         │
│  ┌───────────────▼──────────────────┐     │
│  │  ble_client.c                    │     │
│  │  - Scan, Connect, Subscribe      │     │
│  └───────────────┬──────────────────┘     │
│                  │                         │
│  ┌───────────────▼──────────────────┐     │
│  │  NimBLE Host (nimble/host)       │     │
│  │  - GAP, GATT, SM                 │     │
│  └───────────────┬──────────────────┘     │
│                  │                         │
│  ┌───────────────▼──────────────────┐     │
│  │  ESP-Hosted BLE Transport        │     │
│  │  - HCI over SDIO                 │     │
│  └───────────────┬──────────────────┘     │
│                  │                         │
└──────────────────┼────────────────────────┘
                   │ SDIO
┌──────────────────▼────────────────────────┐
│         ESP32-C6 (Slave)                  │
│                                            │
│  ┌──────────────────────────────────┐     │
│  │  ESP-Hosted Firmware             │     │
│  └───────────────┬──────────────────┘     │
│                  │                         │
│  ┌───────────────▼──────────────────┐     │
│  │  BLE Controller (HCI)            │     │
│  │  - Link Layer, PHY               │     │
│  └───────────────┬──────────────────┘     │
│                  │                         │
└──────────────────┼────────────────────────┘
                   │ BLE Radio (2.4 GHz)
                   │
        ┌──────────▼──────────┐
        │  HR Monitor         │
        │  (Polar, Garmin,    │
        │   Wahoo, etc.)      │
        └─────────────────────┘
```

## Servicio Heart Rate

### UUID del Servicio

**Heart Rate Service**: `0x180D` (16-bit UUID)

### Características

| Característica | UUID | Propiedades | Descripción |
|----------------|------|-------------|-------------|
| Heart Rate Measurement | 0x2A37 | Notify | BPM + flags |
| Body Sensor Location | 0x2A38 | Read | Ubicación del sensor |
| Heart Rate Control Point | 0x2A39 | Write | Reset de contador de energía |

### Formato de Heart Rate Measurement

**Byte 0 (Flags)**:
- Bit 0: Formato (0=uint8, 1=uint16)
- Bit 1-2: Sensor contact (0=no soportado, 2=no contacto, 3=contacto OK)
- Bit 3: Energy expended present
- Bit 4: RR-Interval present

**Byte 1-2**: BPM value (uint8 o uint16 según flags)

**Ejemplo**:
```
[0x00, 0x48] → Flags=0x00 (uint8), BPM=72
[0x01, 0x5A, 0x00] → Flags=0x01 (uint16), BPM=90
```

## Flujo de Operación

### 1. Inicialización

```c
// En main.c
ble_client_init();
```

**Acciones**:
- Inicializa NimBLE host
- Configura callbacks
- Crea tareas NimBLE
- Intenta cargar dispositivo guardado
- Si hay dispositivo guardado, inicia reconexión automática

### 2. Escaneo Manual (desde UI)

```c
// Usuario presiona botón "Scan BLE"
void on_ble_scan_button(void) {
    ble_client_start_scan(device_found_callback);
}

// Callback por cada dispositivo encontrado
void device_found_callback(const char *name, ble_addr_t addr) {
    printf("Encontrado: %s\n", name);
    // Agregar a lista en UI
}
```

**Duración del escaneo**: 10 segundos

**Filtros**:
- Solo dispositivos que anuncian Heart Rate Service (0x180D)
- RSSI > -80 dBm (opcional, configurar en código)

### 3. Conexión

```c
// Usuario selecciona dispositivo de la lista
void on_device_selected(ble_addr_t addr) {
    ble_client_connect(addr);
}
```

**Proceso**:
1. Detener escaneo (si activo)
2. Iniciar conexión GAP
3. Esperar evento `BLE_GAP_EVENT_CONNECT`
4. Descubrir servicios GATT
5. Descubrir características
6. Suscribirse a notificaciones de 0x2A37

**Tiempo típico**: 2-5 segundos

### 4. Recepción de BPM

Cuando el monitor envía notificación:
```c
// En ble_client.c
static int ble_client_on_notify(uint16_t conn_handle, ...) {
    uint8_t bpm = data[1];  // Asumir formato uint8

    // Actualizar estado global
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_treadmill_state.real_pulse = bpm;
        g_treadmill_state.ble_connected = true;
        xSemaphoreGive(g_state_mutex);
    }

    ESP_LOGI(TAG, "Heart Rate: %d bpm", bpm);
}
```

**Frecuencia**: Típicamente 1 Hz (1 notificación por segundo)

### 5. Persistencia (Opcional)

```c
// Guardar dispositivo para reconexión futura
void on_save_device_button(ble_addr_t addr) {
    ble_client_save_device(addr);
}
```

### 6. Reconexión Automática

Si hay dispositivo guardado, al arrancar:
```c
// En ble_client_init()
ble_addr_t saved_addr;
if (ble_client_load_saved_device(&saved_addr)) {
    // Crear tarea de reconexión
    xTaskCreate(ble_reconnect_task, ...);
}
```

**Política de Reintentos**:
- Intento inmediato
- Si falla, esperar 5 segundos
- Reintentar indefinidamente hasta éxito o escaneo manual

## API Pública

### ble_client.h

```c
// Inicialización
void ble_client_init(void);

// Escaneo
typedef void (*ble_device_found_callback_t)(const char *name, ble_addr_t addr);
void ble_client_start_scan(ble_device_found_callback_t cb);

// Conexión
void ble_client_connect(ble_addr_t addr);

// Persistencia
void ble_client_save_device(ble_addr_t addr);
bool ble_client_load_saved_device(ble_addr_t *addr);
```

### Acceso al BPM

```c
// En treadmill_state.h
extern TreadmillState g_treadmill_state;

// Leer BPM (thread-safe)
if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
    uint16_t bpm = g_treadmill_state.real_pulse;
    bool connected = g_treadmill_state.ble_connected;
    xSemaphoreGive(g_state_mutex);

    if (connected) {
        printf("BPM: %d\n", bpm);
    }
}
```

## Persistencia NVS

### Formato de Almacenamiento

**Namespace**: `"ble_client"`
**Key**: `"saved_addr"`

**Contenido** (6 bytes):
```c
typedef struct {
    uint8_t addr[6];    // Dirección MAC BLE
    uint8_t type;       // Tipo (PUBLIC=0, RANDOM=1)
} __attribute__((packed)) saved_ble_addr_t;
```

**Ejemplo**:
```
Dirección: 01:23:45:67:89:AB
Tipo: RANDOM (1)
NVS value: [0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0x01]
```

### Funciones de Persistencia

```c
void ble_client_save_device(ble_addr_t addr) {
    nvs_handle_t nvs_h;
    nvs_open("ble_client", NVS_READWRITE, &nvs_h);

    saved_ble_addr_t saved;
    memcpy(saved.addr, addr.val, 6);
    saved.type = addr.type;

    nvs_set_blob(nvs_h, "saved_addr", &saved, sizeof(saved));
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

bool ble_client_load_saved_device(ble_addr_t *addr) {
    nvs_handle_t nvs_h;
    if (nvs_open("ble_client", NVS_READONLY, &nvs_h) != ESP_OK) {
        return false;
    }

    saved_ble_addr_t saved;
    size_t len = sizeof(saved);
    esp_err_t err = nvs_get_blob(nvs_h, "saved_addr", &saved, &len);

    nvs_close(nvs_h);

    if (err == ESP_OK) {
        memcpy(addr->val, saved.addr, 6);
        addr->type = saved.type;
        return true;
    }
    return false;
}
```

## Reconexión Automática

### Tarea de Reconexión

```c
static void ble_reconnect_task(void *pvParameters) {
    ble_addr_t saved_addr;

    if (!ble_client_load_saved_device(&saved_addr)) {
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "Attempting reconnection...");
            ble_client_connect(saved_addr);
        }

        // Esperar 5 segundos antes de reintentar
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

**Condiciones para Detener Reconexión**:
- Usuario inicia escaneo manual
- Usuario desconecta manualmente
- Conexión exitosa (la tarea espera desconexión)

## Ejemplos de Uso

### Ejemplo 1: Flujo Completo desde UI

```c
// 1. Usuario presiona "Scan BLE"
void ui_ble_scan_button_pressed(void) {
    ble_client_start_scan(ui_ble_device_found);
}

// 2. Callback por cada dispositivo encontrado
void ui_ble_device_found(const char *name, ble_addr_t addr) {
    // Agregar a lista de dispositivos en UI
    add_device_to_ui_list(name, addr);
}

// 3. Usuario selecciona dispositivo de la lista
void ui_ble_device_selected(int index) {
    ble_addr_t addr = get_device_addr_from_ui_list(index);
    ble_client_connect(addr);

    // Opcional: guardar para reconexión futura
    ble_client_save_device(addr);
}

// 4. Mostrar BPM en UI (tarea periódica)
void ui_update_task(void *param) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (g_treadmill_state.ble_connected) {
                uint16_t bpm = g_treadmill_state.real_pulse;
                xSemaphoreGive(g_state_mutex);

                // Actualizar label de BPM
                update_bpm_label(bpm);
            } else {
                xSemaphoreGive(g_state_mutex);
                show_bpm_disconnected();
            }
        }
    }
}
```

### Ejemplo 2: Reconexión Automática al Arrancar

```c
// En main.c
void app_main(void) {
    // ... otras inicializaciones

    ble_client_init();  // Automáticamente intenta reconectar
                        // si hay dispositivo guardado

    // Continuar con UI...
}
```

## Troubleshooting

### No encuentra dispositivos

**Síntomas**:
- Escaneo termina sin dispositivos
- Log `"Scan complete. Found 0 devices."`

**Soluciones**:
1. Verificar que el monitor HR está encendido y en modo pairing
2. Confirmar que el monitor anuncia servicio 0x180D
3. Acercar el monitor al ESP32-P4 (< 1 metro)
4. Verificar firmware ESP-Hosted en C6 incluye BLE
5. Reintentar el escaneo

### Conexión falla

**Síntomas**:
- Log `"Connection failed"`
- `BLE_GAP_EVENT_CONNECT` con status != 0

**Causas**:
- Dispositivo fuera de rango
- Dispositivo ya conectado a otro host
- Dirección MAC incorrecta

**Soluciones**:
- Reiniciar el monitor HR
- Desemparejar de otros dispositivos (smartphone, etc.)
- Repetir escaneo y asegurar dirección correcta

### No recibe BPM

**Síntomas**:
- Conectado pero `g_treadmill_state.real_pulse = 0`
- No hay logs de `"Heart Rate: X bpm"`

**Causas**:
- Monitor HR no envía notificaciones
- Característica 0x2A37 no suscrita correctamente
- Monitor requiere autenticación/bonding

**Soluciones**:
1. Verificar logs de descubrimiento GATT
2. Confirmar suscripción a 0x2A37
3. Algunos monitores requieren bonding (implementar Security Manager si necesario)

### Reconexión automática no funciona

**Síntomas**:
- Al arrancar, no se conecta automáticamente
- No hay logs de `"Attempting reconnection"`

**Soluciones**:
1. Verificar que el dispositivo fue guardado:
   ```c
   ble_addr_t addr;
   if (ble_client_load_saved_device(&addr)) {
       printf("Saved device found: %02X:%02X:...\n", addr.val[0], addr.val[1]);
   } else {
       printf("No saved device\n");
   }
   ```

2. Confirmar que g_user_initiated_disconnect = false
3. Verificar que la tarea de reconexión se creó

### BPM incorrecto o errático

**Síntomas**:
- BPM salta bruscamente (ej: 80 → 200 → 70)
- Valores imposibles (< 30 o > 250)

**Causas**:
- Mal contacto del sensor
- Batería baja del monitor
- Interferencia electromagnética

**Soluciones**:
- Ajustar banda del monitor
- Cambiar batería
- Alejar de fuentes de EMI (microondas, WiFi de alta potencia)
- Implementar filtro en código:
  ```c
  if (bpm >= 30 && bpm <= 250) {
      g_treadmill_state.real_pulse = bpm;
  }
  ```

## Logs y Debugging

### Etiquetas de Log

- `NIMBLE_BLE_CLIENT`: Cliente BLE general

### Mensajes Clave

**Inicialización**:
```
I (xxxx) NIMBLE_BLE_CLIENT: BLE Host initialized
I (xxxx) NIMBLE_BLE_CLIENT: Loading saved device from NVS...
I (xxxx) NIMBLE_BLE_CLIENT: Saved device found: 01:23:45:67:89:AB
```

**Escaneo**:
```
I (xxxx) NIMBLE_BLE_CLIENT: Starting BLE scan for HR devices...
I (xxxx) NIMBLE_BLE_CLIENT: Found device: Polar H10 (RSSI: -55 dBm)
I (xxxx) NIMBLE_BLE_CLIENT: Scan complete. Found 3 devices.
```

**Conexión**:
```
I (xxxx) NIMBLE_BLE_CLIENT: Connecting to device...
I (xxxx) NIMBLE_BLE_CLIENT: Connected to device
I (xxxx) NIMBLE_BLE_CLIENT: Discovering services...
I (xxxx) NIMBLE_BLE_CLIENT: Subscribing to HR notifications...
I (xxxx) NIMBLE_BLE_CLIENT: Subscribed successfully
```

**BPM**:
```
I (xxxx) NIMBLE_BLE_CLIENT: Heart Rate: 72 bpm
I (xxxx) NIMBLE_BLE_CLIENT: Heart Rate: 75 bpm
I (xxxx) NIMBLE_BLE_CLIENT: Heart Rate: 78 bpm
```

**Errores**:
```
W (xxxx) NIMBLE_BLE_CLIENT: Connection failed (status=3)
E (xxxx) NIMBLE_BLE_CLIENT: Failed to discover services
W (xxxx) NIMBLE_BLE_CLIENT: Disconnected from device
```

## Referencias

- **NimBLE**: https://github.com/apache/mynewt-nimble
- **Heart Rate Profile**: https://www.bluetooth.com/specifications/specs/heart-rate-profile-1-0/
- **ESP-Hosted**: https://github.com/espressif/esp-hosted

---

**Última actualización**: 2025-11-05
