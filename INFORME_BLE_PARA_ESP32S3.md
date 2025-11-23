# Informe: Replicación de Lógica BLE en ESP32-S3

**Proyecto Origen**: Treadmill Console (ESP32-P4)
**Proyecto Destino**: ESP32-S3
**Fecha**: 2025-11-23
**Objetivo**: Replicar toda la funcionalidad BLE de cliente para monitores de frecuencia cardíaca

---

## Tabla de Contenidos

1. [Resumen Ejecutivo](#resumen-ejecutivo)
2. [Diferencias Arquitectónicas Clave](#diferencias-arquitectónicas-clave)
3. [Componentes y Dependencias](#componentes-y-dependencias)
4. [Estructura de Archivos](#estructura-de-archivos)
5. [Configuración del Proyecto](#configuración-del-proyecto)
6. [Implementación Detallada](#implementación-detallada)
7. [Integración con el Sistema](#integración-con-el-sistema)
8. [Testing y Validación](#testing-y-validación)
9. [Troubleshooting](#troubleshooting)

---

## 1. Resumen Ejecutivo

### ¿Qué hace el sistema BLE actual?

El proyecto actual implementa un **cliente BLE** que:
- Escanea y se conecta a monitores de frecuencia cardíaca BLE
- Recibe datos de pulso cardíaco (BPM) en tiempo real
- Guarda dispositivos favoritos para reconexión automática
- Implementa reconexión automática ante desconexiones
- Integra los datos BPM en el estado global de la aplicación

### Arquitectura Actual (ESP32-P4)

```
ESP32-P4 (sin BLE) ←→ ESP-Hosted (SDIO) ←→ ESP32-C6 (con BLE) ←→ Monitor HR
    ↑
NimBLE Host Stack
```

### Arquitectura Objetivo (ESP32-S3)

```
ESP32-S3 (con BLE integrado) ←→ Monitor HR
    ↑
NimBLE Host + Controller Stack
```

**Ventaja**: El ESP32-S3 tiene radio BLE integrado, por lo que NO necesita ESP-Hosted. La implementación será más simple y directa.

---

## 2. Diferencias Arquitectónicas Clave

### ESP32-P4 (Proyecto Actual)

| Característica | Detalle |
|----------------|---------|
| **Radio BLE** | ❌ No tiene |
| **Solución** | ESP-Hosted con ESP32-C6 externo |
| **Transporte** | SDIO |
| **Stack BLE** | Solo NimBLE Host |
| **Controlador** | Remoto (en C6) |
| **Complejidad** | Alta (2 chips) |

### ESP32-S3 (Proyecto Destino)

| Característica | Detalle |
|----------------|---------|
| **Radio BLE** | ✅ Integrado |
| **Solución** | Nativo |
| **Transporte** | N/A |
| **Stack BLE** | NimBLE Host + Controller |
| **Controlador** | Local |
| **Complejidad** | Baja (1 chip) |

### Cambios Requeridos

1. ❌ **Eliminar**: `esp_hosted_init()` y dependencia de ESP-Hosted
2. ✅ **Mantener**: Todo el código de NimBLE Host
3. ✅ **Agregar**: Configuración del controlador BLE local
4. ✅ **Simplificar**: Inicialización (sin capa de transporte)

---

## 3. Componentes y Dependencias

### 3.1 Dependencias IDF Component Manager

**Archivo**: `idf_component.yml` (en el directorio raíz del proyecto)

```yaml
dependencies:
  espressif/nimble: '*'
  # NO incluir esp_hosted ni esp_wifi_remote para ESP32-S3
```

### 3.2 Componentes ESP-IDF Requeridos

**Archivo**: `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.c"
         "ble_client.c"
         "treadmill_state.c"
         # ... otros archivos
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        bt           # Componente Bluetooth de ESP-IDF
        # NO incluir esp_hosted ni esp_wifi_remote
)
```

### 3.3 Configuración Kconfig (sdkconfig)

**Archivo**: `sdkconfig.defaults` (crear si no existe)

```ini
# ===== Bluetooth Configuration =====
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=n

# Habilitar controlador BLE local (diferencia clave vs P4)
CONFIG_BT_CONTROLLER_ENABLED=y

# Configuración NimBLE
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y
CONFIG_BT_NIMBLE_LOG_LEVEL_INFO=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
CONFIG_BT_NIMBLE_MAX_BONDS=3
CONFIG_BT_NIMBLE_MAX_CCCDS=8

# Roles BLE
CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y

# Cliente/Servidor GATT
CONFIG_BT_NIMBLE_GATT_CLIENT=y
CONFIG_BT_NIMBLE_GATT_SERVER=y

# Seguridad
CONFIG_BT_NIMBLE_SECURITY_ENABLE=y
CONFIG_BT_NIMBLE_SM_LEGACY=y
CONFIG_BT_NIMBLE_SM_SC=y

# Tamaño de stack
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096

# ATT/GATT
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256
CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES=64

# Buffers
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=12
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE=256
CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=24
CONFIG_BT_NIMBLE_MSYS_2_BLOCK_SIZE=320
CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT=24
CONFIG_BT_NIMBLE_TRANSPORT_ACL_SIZE=255

# ===== NVS Configuration =====
CONFIG_NVS_ENCRYPTION=n
```

---

## 4. Estructura de Archivos

### Archivos a Copiar/Crear

```
proyecto_esp32s3/
├── main/
│   ├── CMakeLists.txt          # Modificado (sin esp_hosted)
│   ├── main.c                  # Modificado (inicialización simplificada)
│   ├── ble_client.c            # Copiado + modificado
│   ├── ble_client.h            # Copiado sin cambios
│   ├── treadmill_state.c       # Copiado sin cambios (o adaptado)
│   └── treadmill_state.h       # Copiado sin cambios (o adaptado)
├── idf_component.yml           # Nuevo (solo nimble)
├── sdkconfig.defaults          # Nuevo (config BLE)
└── CMakeLists.txt              # Estándar ESP-IDF
```

---

## 5. Configuración del Proyecto

### 5.1 CMakeLists.txt Principal

**Archivo**: `CMakeLists.txt` (raíz del proyecto)

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_ble_heart_rate)
```

### 5.2 idf_component.yml

**Archivo**: `idf_component.yml`

```yaml
## IDF Component Manager Manifest File
dependencies:
  espressif/nimble: '*'
```

### 5.3 Configuración menuconfig

Después de copiar `sdkconfig.defaults`, ejecutar:

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Verificar en `Component config → Bluetooth`:
- ✅ Bluetooth enabled
- ✅ NimBLE enabled
- ✅ Controller enabled (importante para S3)
- ❌ Bluedroid disabled

---

## 6. Implementación Detallada

### 6.1 Archivo: `ble_client.h`

**Ubicación**: `main/ble_client.h`
**Modificaciones**: ❌ Ninguna (copiar tal cual)

```c
#ifndef BLE_CLIENT_H
#define BLE_CLIENT_H

#include <stdint.h>
#include "host/ble_hs.h"

// Callback para notificar dispositivos encontrados
typedef void (*ble_device_found_callback_t)(const char* name, ble_addr_t addr);

/**
 * @brief Inicializa el cliente BLE
 */
void ble_client_init(void);

/**
 * @brief Inicia escaneo de dispositivos Heart Rate
 * @param cb Callback para cada dispositivo encontrado
 */
void ble_client_start_scan(ble_device_found_callback_t cb);

/**
 * @brief Conecta a un dispositivo BLE específico
 * @param addr Dirección del dispositivo
 */
void ble_client_connect(ble_addr_t addr);

/**
 * @brief Guarda dispositivo en NVS para reconexión
 * @param addr Dirección del dispositivo
 */
void ble_client_save_device(ble_addr_t addr);

/**
 * @brief Carga dispositivo guardado desde NVS
 * @param addr Puntero para almacenar la dirección
 * @return true si se cargó exitosamente
 */
bool ble_client_load_saved_device(ble_addr_t *addr);

#endif // BLE_CLIENT_H
```

---

### 6.2 Archivo: `ble_client.c` (Versión ESP32-S3)

**Ubicación**: `main/ble_client.c`
**Modificaciones**: ⚠️ Cambiar inicialización

#### Cambios Principales

1. **Eliminar**: `#include "esp_hosted.h"`
2. **Eliminar**: Llamadas a `esp_hosted_init()`
3. **Agregar**: Inicialización del controlador BLE local
4. **Mantener**: Todo el código de NimBLE Host sin cambios

#### Código Completo Modificado

```c
/*
 * BLE Client for Heart Rate Monitor using NimBLE on ESP32-S3
 *
 * Adaptado desde ESP32-P4 + ESP-Hosted a ESP32-S3 con BLE nativo
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdbool.h>

// Core NimBLE includes
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// *** NUEVO PARA ESP32-S3: Incluir controlador BLE ***
#include "esp_nimble_hci.h"

#include "treadmill_state.h"
#include "ble_client.h"

static const char *TAG = "NIMBLE_BLE_CLIENT";

// NVS constants
#define NVS_NAMESPACE "ble_client"
#define NVS_KEY_SAVED_ADDR "saved_addr"

// Heart Rate Service UUIDs
static const ble_uuid16_t g_svc_heart_rate_uuid = BLE_UUID16_INIT(0x180D);
static const ble_uuid16_t g_chr_heart_rate_meas_uuid = BLE_UUID16_INIT(0x2A37);

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_hr_chr_val_handle = 0;
static uint8_t g_own_addr_type;

// Globals para escaneo y lista de dispositivos
#define MAX_DISCOVERED_DEVICES 20
static ble_addr_t g_discovered_devices[MAX_DISCOVERED_DEVICES];
static int g_discovered_device_count = 0;
static ble_device_found_callback_t g_device_found_cb = NULL;
static bool g_is_scanning = false;
static bool g_user_initiated_disconnect = false;
static TaskHandle_t g_reconnect_task_handle = NULL;
static SemaphoreHandle_t g_ble_state_mutex = NULL;

// Forward declarations
static void ble_reconnect_task(void *pvParameters);
static void ble_client_scan_internal(void);
static int ble_client_gap_event(struct ble_gap_event *event, void *arg);
static void ble_client_on_sync(void);
static void ble_client_on_reset(int reason);
static void ble_host_task(void *param);
static int ble_client_on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                   uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg);
static int ble_client_on_char_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                    const struct ble_gatt_chr *chr, void *arg);
static int ble_client_on_service_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                       const struct ble_gatt_svc *service, void *arg);

// ==================== FUNCIONES PÚBLICAS ====================

void ble_client_start_scan(ble_device_found_callback_t cb) {
    bool is_scanning = false;
    if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        is_scanning = g_is_scanning;
        xSemaphoreGive(g_ble_state_mutex);
    }

    if (is_scanning) {
        ESP_LOGW(TAG, "Scan already in progress.");
        return;
    }

    // Desconectar si está conectado
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "Disconnecting from current device to start scan...");
        if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_user_initiated_disconnect = true;
            xSemaphoreGive(g_ble_state_mutex);
        }
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_hr_chr_val_handle = 0;
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_treadmill_state.ble_connected = false;
            g_treadmill_state.real_pulse = 0;
            xSemaphoreGive(g_state_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Starting new scan.");
    g_device_found_cb = cb;
    g_discovered_device_count = 0;
    memset(g_discovered_devices, 0, sizeof(g_discovered_devices));
    ble_client_scan_internal();
}

void ble_client_connect(ble_addr_t addr) {
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Already connected or connecting.");
        return;
    }

    // Detener escaneo antes de conectar
    bool was_scanning = false;
    if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        was_scanning = g_is_scanning;
        xSemaphoreGive(g_ble_state_mutex);
    }

    if (was_scanning) {
        int rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGE(TAG, "Failed to cancel scan before connecting; rc=%d", rc);
        }
        if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_is_scanning = false;
            xSemaphoreGive(g_ble_state_mutex);
        }
    }

    ESP_LOGI(TAG, "Attempting to connect to device with address: %02x:%02x:%02x:%02x:%02x:%02x",
             addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1], addr.val[0]);

    if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_user_initiated_disconnect = false;
        xSemaphoreGive(g_ble_state_mutex);
    }

    int rc = ble_gap_connect(g_own_addr_type, &addr, 30000, NULL, ble_client_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to initiate connect; rc=%d. Restarting scan.", rc);
        ble_client_scan_internal();
    }
}

void ble_client_save_device(ble_addr_t addr) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY_SAVED_ADDR, &addr, sizeof(ble_addr_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write address to NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Saved device address to NVS.");
        nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
}

bool ble_client_load_saved_device(ble_addr_t *addr) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, no device saved yet.");
        return false;
    }

    size_t required_size = sizeof(ble_addr_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_SAVED_ADDR, addr, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read saved address from NVS: %s", esp_err_to_name(err));
        return false;
    }

    if (required_size != sizeof(ble_addr_t)) {
        ESP_LOGW(TAG, "NVS blob size mismatch, ignoring saved address.");
        return false;
    }

    ESP_LOGI(TAG, "Successfully loaded saved device address from NVS.");
    return true;
}

// ==================== FUNCIONES INTERNAS ====================

static int ble_client_gap_event(struct ble_gap_event *event, void *arg) {
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        // Verificar si anuncia Heart Rate Service
        bool hr_service_found = false;
        for (int i = 0; i < fields.num_uuids16; i++) {
            if (ble_uuid_cmp(&fields.uuids16[i].u, &g_svc_heart_rate_uuid.u) == 0) {
                hr_service_found = true;
                break;
            }
        }

        if (hr_service_found) {
            // Verificar si ya fue descubierto
            for (int i = 0; i < g_discovered_device_count; i++) {
                if (ble_addr_cmp(&event->disc.addr, &g_discovered_devices[i]) == 0) {
                    return 0;
                }
            }

            // Agregar a lista
            if (g_discovered_device_count < MAX_DISCOVERED_DEVICES) {
                g_discovered_devices[g_discovered_device_count++] = event->disc.addr;

                char dev_name[30] = {0};
                if (fields.name != NULL && fields.name_len > 0) {
                    int name_len = fields.name_len > sizeof(dev_name) - 1 ? sizeof(dev_name) - 1 : fields.name_len;
                    memcpy(dev_name, fields.name, name_len);
                } else {
                    snprintf(dev_name, sizeof(dev_name), "HRM-%02x%02x", event->disc.addr.val[1], event->disc.addr.val[0]);
                }

                ESP_LOGI(TAG, "Found Heart Rate device: %s", dev_name);

                if (g_device_found_cb) {
                    g_device_found_cb(dev_name, event->disc.addr);
                }
            }
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        g_is_scanning = false;
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Connection established; conn_handle=%d", event->connect.conn_handle);
            g_conn_handle = event->connect.conn_handle;

            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_treadmill_state.ble_connected = true;
                xSemaphoreGive(g_state_mutex);
            }

            // Guardar dispositivo conectado
            struct ble_gap_conn_desc desc;
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0) {
                ble_client_save_device(desc.peer_id_addr);
            }

            // Descubrir servicios
            rc = ble_gattc_disc_all_svcs(g_conn_handle, ble_client_on_service_disc, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to discover services; rc=%d", rc);
            }
        } else {
            ESP_LOGE(TAG, "Connection attempt failed; status=%d", event->connect.status);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_hr_chr_val_handle = 0;

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_treadmill_state.ble_connected = false;
            g_treadmill_state.real_pulse = 0;
            xSemaphoreGive(g_state_mutex);
        }

        // Auto-reconexión
        if (!g_user_initiated_disconnect) {
            ble_addr_t saved_addr;
            if (ble_client_load_saved_device(&saved_addr)) {
                ESP_LOGI(TAG, "Attempting to reconnect to saved device...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                ble_client_connect(saved_addr);
            }
        } else {
            ESP_LOGI(TAG, "User initiated disconnect - not auto-reconnecting.");
            g_user_initiated_disconnect = false;
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        g_is_scanning = false;
        ESP_LOGI(TAG, "Scan complete; reason=%d", event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == g_hr_chr_val_handle) {
            uint8_t *data = event->notify_rx.om->om_data;
            uint16_t len = event->notify_rx.om->om_len;

            if (len >= 2) {
                uint8_t flags = data[0];
                uint16_t bpm = (flags & 0x01) ? ((data[2] << 8) | data[1]) : data[1];

                if (bpm > 30 && bpm < 250) {
                    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        g_treadmill_state.real_pulse = bpm;
                        xSemaphoreGive(g_state_mutex);
                    }
                }
            }
        }
        return 0;

    default:
        return 0;
    }
}

static int ble_client_on_service_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                    const struct ble_gatt_svc *service, void *arg) {
    if (error->status == 0 && service != NULL) {
        if (ble_uuid_cmp(&service->uuid.u, &g_svc_heart_rate_uuid.u) == 0) {
            ESP_LOGI(TAG, "Found Heart Rate Service. Discovering characteristics...");
            ble_gattc_disc_all_chrs(conn_handle, service->start_handle, service->end_handle,
                                    ble_client_on_char_disc, NULL);
        }
    }
    return 0;
}

static int ble_client_on_char_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                 const struct ble_gatt_chr *chr, void *arg) {
    if (error->status == 0 && chr != NULL) {
        if (ble_uuid_cmp(&chr->uuid.u, &g_chr_heart_rate_meas_uuid.u) == 0) {
            ESP_LOGI(TAG, "Found Heart Rate Measurement characteristic.");
            g_hr_chr_val_handle = chr->val_handle;
            ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, 0xffff, ble_client_on_dsc_disc, NULL);
        }
    }
    return 0;
}

static int ble_client_on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error *error,
                                uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg) {
    if (error->status == 0 && dsc != NULL) {
        if (dsc->uuid.u.type == BLE_UUID_TYPE_16 && ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            ESP_LOGI(TAG, "Found CCCD for HR Measurement. Enabling notifications.");
            uint8_t value[2] = {0x01, 0x00};
            ble_gattc_write_flat(conn_handle, dsc->handle, value, sizeof(value), NULL, NULL);
        }
    }
    return 0;
}

static void ble_reconnect_task(void *pvParameters) {
    ble_addr_t saved_addr;
    bool has_saved_device = false;

    ESP_LOGI(TAG, "Reconnect task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        bool should_skip = false;
        if (g_ble_state_mutex && xSemaphoreTake(g_ble_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            should_skip = g_is_scanning || g_user_initiated_disconnect;
            xSemaphoreGive(g_ble_state_mutex);
        }
        if (should_skip) {
            continue;
        }

        if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            continue;
        }

        if (!has_saved_device) {
            has_saved_device = ble_client_load_saved_device(&saved_addr);
            if (!has_saved_device) {
                continue;
            }
        }

        ESP_LOGI(TAG, "Not connected - attempting reconnect to saved device...");
        ble_client_connect(saved_addr);
    }
}

static void ble_client_on_sync(void) {
    int rc;
    ESP_LOGI(TAG, "BLE Host synced.");

    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    // Auto-conectar a dispositivo guardado
    ble_addr_t saved_addr;
    if (ble_client_load_saved_device(&saved_addr)) {
        ble_client_connect(saved_addr);
    } else {
        ESP_LOGI(TAG, "No saved device found. Waiting for user to initiate scan.");
    }

    // Iniciar tarea de reconexión
    if (g_reconnect_task_handle == NULL) {
        xTaskCreate(ble_reconnect_task, "ble_reconnect", 4096, NULL, 5, &g_reconnect_task_handle);
        ESP_LOGI(TAG, "BLE reconnect task started");
    }
}

static void ble_client_on_reset(int reason) {
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void ble_client_scan_internal(void) {
    struct ble_gap_disc_params disc_params;
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int rc = ble_gap_disc(g_own_addr_type, 10000, &disc_params, ble_client_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error starting scan; rc=%d", rc);
        g_is_scanning = false;
    } else {
        g_is_scanning = true;
        ESP_LOGI(TAG, "Starting BLE scan for Heart Rate sensors...");
    }
}

void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ==================== INICIALIZACIÓN (MODIFICADO PARA ESP32-S3) ====================

void ble_client_init(void) {
    ESP_LOGI(TAG, "Initializing BLE Client for ESP32-S3...");

    // Crear mutex de estado BLE
    if (g_ble_state_mutex == NULL) {
        g_ble_state_mutex = xSemaphoreCreateMutex();
        if (g_ble_state_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create BLE state mutex");
            return;
        }
    }

    // *** NUEVO PARA ESP32-S3: Inicializar controlador BLE local ***
    ESP_LOGI(TAG, "Initializing BLE controller for ESP32-S3...");
    esp_err_t ret = esp_nimble_hci_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE HCI: %s", esp_err_to_name(ret));
        return;
    }

    // Inicializar puerto NimBLE
    nimble_port_init();

    // Configurar callbacks
    ble_hs_cfg.sync_cb = ble_client_on_sync;
    ble_hs_cfg.reset_cb = ble_client_on_reset;

    // Configuración de seguridad (sin input/output)
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 0;

    // Configurar nombre del dispositivo
    const char *device_name = "esp32s3-hr-client";
    ble_svc_gap_device_name_set(device_name);

    // Iniciar tarea de host NimBLE
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Client initialization complete.");
}
```

---

### 6.3 Archivo: `main.c` (Versión ESP32-S3)

**Ubicación**: `main/main.c`
**Modificaciones**: ⚠️ Simplificar inicialización

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "treadmill_state.h"
#include "ble_client.h"

static const char *TAG = "MainApp";

void app_main(void) {
    // Inicializar NVS (requerido para BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // *** NO NECESARIO PARA ESP32-S3: esp_hosted_init() ***
    // El ESP32-S3 tiene BLE integrado, no necesita transporte externo

    // Crear mutex de estado global
    g_state_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex. Aborting.");
        abort();
    }

    // Inicializar cliente BLE
    ESP_LOGI(TAG, "Initializing BLE Client...");
    ble_client_init();

    // Aquí puedes agregar inicialización de UI, sensores, etc.

    ESP_LOGI(TAG, "Initialization complete.");

    // Ejemplo: Tarea de prueba que imprime BPM cada segundo
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (g_treadmill_state.ble_connected) {
                ESP_LOGI(TAG, "Heart Rate: %d BPM", g_treadmill_state.real_pulse);
            } else {
                ESP_LOGI(TAG, "BLE not connected");
            }
            xSemaphoreGive(g_state_mutex);
        }
    }
}
```

---

### 6.4 Archivo: `treadmill_state.h` y `treadmill_state.c`

**Ubicación**: `main/treadmill_state.h` y `main/treadmill_state.c`

Estos archivos se pueden copiar directamente del proyecto original, ajustando solo los campos que necesites. El núcleo de la estructura BLE es:

```c
// En treadmill_state.h
typedef struct {
    // ... otros campos

    // Datos BLE
    volatile uint16_t real_pulse;
    volatile bool ble_connected;

    // ... otros campos
} TreadmillState;

extern TreadmillState g_treadmill_state;
extern SemaphoreHandle_t g_state_mutex;
```

```c
// En treadmill_state.c
#include "treadmill_state.h"

TreadmillState g_treadmill_state = {
    .real_pulse = 0,
    .ble_connected = false,
    // ... inicializar otros campos
};

SemaphoreHandle_t g_state_mutex = NULL;
```

---

## 7. Integración con el Sistema

### 7.1 Flujo de Inicialización

```
app_main()
   │
   ├─→ nvs_flash_init()              // Inicializar NVS
   │
   ├─→ g_state_mutex = xSemaphoreCreateMutex()  // Mutex global
   │
   └─→ ble_client_init()
        │
        ├─→ esp_nimble_hci_init()    // *** SOLO ESP32-S3 ***
        ├─→ nimble_port_init()
        ├─→ configurar callbacks
        └─→ nimble_port_freertos_init()
             │
             └─→ ble_host_task()
                  │
                  └─→ ble_client_on_sync()
                       │
                       ├─→ cargar dispositivo guardado
                       ├─→ auto-conectar (si existe)
                       └─→ iniciar tarea de reconexión
```

### 7.2 Uso desde la Aplicación

#### Ejemplo: Escanear y Conectar desde UI

```c
// Callback para dispositivos encontrados
void my_device_found_callback(const char *name, ble_addr_t addr) {
    printf("Found device: %s (MAC: %02x:%02x:...)\n",
           name, addr.val[5], addr.val[4]);

    // Agregar a lista en UI para que usuario seleccione
    ui_add_device_to_list(name, addr);
}

// Botón "Scan"
void on_scan_button_pressed(void) {
    ble_client_start_scan(my_device_found_callback);
}

// Usuario selecciona dispositivo de la lista
void on_device_selected(ble_addr_t addr) {
    ble_client_connect(addr);
    ble_client_save_device(addr);  // Guardar para reconexión futura
}

// Leer BPM en tarea periódica
void ui_update_task(void *param) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (g_treadmill_state.ble_connected) {
                uint16_t bpm = g_treadmill_state.real_pulse;
                xSemaphoreGive(g_state_mutex);

                printf("BPM: %d\n", bpm);
                // Actualizar UI
            } else {
                xSemaphoreGive(g_state_mutex);
                printf("BLE disconnected\n");
            }
        }
    }
}
```

---

## 8. Testing y Validación

### 8.1 Prueba Básica: Logs de Inicialización

Después de compilar y flashear, deberías ver:

```
I (xxxx) MainApp: Initializing BLE Client...
I (xxxx) NIMBLE_BLE_CLIENT: Initializing BLE Client for ESP32-S3...
I (xxxx) NIMBLE_BLE_CLIENT: Initializing BLE controller for ESP32-S3...
I (xxxx) NIMBLE_BLE_CLIENT: BLE Host Task Started
I (xxxx) NIMBLE_BLE_CLIENT: BLE Host synced.
I (xxxx) NIMBLE_BLE_CLIENT: No saved device found. Waiting for user to initiate scan.
I (xxxx) NIMBLE_BLE_CLIENT: BLE reconnect task started
```

### 8.2 Prueba de Escaneo

```c
// En app_main() o en un botón
void test_scan(void) {
    ble_client_start_scan(test_callback);
}

void test_callback(const char *name, ble_addr_t addr) {
    ESP_LOGI("TEST", "Found: %s", name);
}
```

Logs esperados:

```
I (xxxx) NIMBLE_BLE_CLIENT: Starting new scan.
I (xxxx) NIMBLE_BLE_CLIENT: Starting BLE scan for Heart Rate sensors...
I (xxxx) NIMBLE_BLE_CLIENT: Found Heart Rate device: Polar H10
I (xxxx) TEST: Found: Polar H10
I (xxxx) NIMBLE_BLE_CLIENT: Scan complete; reason=0
```

### 8.3 Prueba de Conexión y BPM

```c
// Después de escanear, conectar al primer dispositivo
void test_connect(ble_addr_t addr) {
    ble_client_connect(addr);
}

// En tarea de monitoreo
void monitor_task(void *param) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool connected = g_treadmill_state.ble_connected;
            uint16_t bpm = g_treadmill_state.real_pulse;
            xSemaphoreGive(g_state_mutex);

            ESP_LOGI("MONITOR", "Connected: %s, BPM: %d",
                     connected ? "YES" : "NO", bpm);
        }
    }
}
```

Logs esperados:

```
I (xxxx) NIMBLE_BLE_CLIENT: Attempting to connect to device with address: xx:xx:xx:xx:xx:xx
I (xxxx) NIMBLE_BLE_CLIENT: Connection established; conn_handle=0
I (xxxx) NIMBLE_BLE_CLIENT: Saved device address to NVS.
I (xxxx) NIMBLE_BLE_CLIENT: Found Heart Rate Service. Discovering characteristics...
I (xxxx) NIMBLE_BLE_CLIENT: Found Heart Rate Measurement characteristic.
I (xxxx) NIMBLE_BLE_CLIENT: Found CCCD for HR Measurement. Enabling notifications.
I (xxxx) MONITOR: Connected: YES, BPM: 72
I (xxxx) MONITOR: Connected: YES, BPM: 75
I (xxxx) MONITOR: Connected: YES, BPM: 78
```

### 8.4 Prueba de Reconexión Automática

1. Conectar a un monitor HR y verificar que recibe BPM
2. Guardar dispositivo con `ble_client_save_device()`
3. Reiniciar el ESP32-S3
4. Verificar que automáticamente se reconecta al arrancar

Logs esperados:

```
I (xxxx) NIMBLE_BLE_CLIENT: BLE Host synced.
I (xxxx) NIMBLE_BLE_CLIENT: Successfully loaded saved device address from NVS.
I (xxxx) NIMBLE_BLE_CLIENT: Attempting to connect to device with address: xx:xx:xx:xx:xx:xx
I (xxxx) NIMBLE_BLE_CLIENT: Connection established; conn_handle=0
```

---

## 9. Troubleshooting

### 9.1 Error: "Failed to initialize NimBLE HCI"

**Síntomas**:
```
E (xxxx) NIMBLE_BLE_CLIENT: Failed to initialize NimBLE HCI: ESP_ERR_INVALID_STATE
```

**Causas**:
- `CONFIG_BT_CONTROLLER_ENABLED` no está habilitado
- Controlador BLE ya fue inicializado en otro lugar

**Soluciones**:
1. Verificar `sdkconfig`:
   ```
   CONFIG_BT_ENABLED=y
   CONFIG_BT_CONTROLLER_ENABLED=y
   ```
2. Asegurar que `esp_nimble_hci_init()` se llama solo una vez
3. Borrar build y recompilar:
   ```bash
   idf.py fullclean
   idf.py build
   ```

### 9.2 No encuentra dispositivos al escanear

**Síntomas**:
- Escaneo termina sin encontrar dispositivos
- Monitor HR está encendido y visible desde smartphone

**Causas**:
- Filtro UUID muy restrictivo
- Monitor no anuncia servicio 0x180D en advertising

**Soluciones**:
1. Verificar que el monitor HR anuncia Heart Rate Service
2. Usar app como "nRF Connect" para verificar servicios anunciados
3. Temporalmente eliminar filtro para ver todos los dispositivos:
   ```c
   // En ble_client_gap_event(), caso BLE_GAP_EVENT_DISC
   // Comentar verificación de hr_service_found
   ```

### 9.3 Se conecta pero no recibe BPM

**Síntomas**:
- Conexión exitosa
- `ble_connected = true`
- Pero `real_pulse` siempre es 0

**Causas**:
- Notificaciones no habilitadas correctamente
- Monitor requiere bonding/autenticación
- CCCD no escrito

**Soluciones**:
1. Verificar logs de descubrimiento GATT:
   ```
   I (xxxx) Found Heart Rate Service
   I (xxxx) Found Heart Rate Measurement characteristic
   I (xxxx) Found CCCD for HR Measurement. Enabling notifications.
   ```
2. Verificar que no hay errores en `ble_gattc_write_flat()`
3. Algunos monitores requieren bonding - implementar si necesario

### 9.4 Error de compilación: "undefined reference to esp_nimble_hci_init"

**Causas**:
- Falta componente `bt` en `CMakeLists.txt`

**Solución**:
```cmake
idf_component_register(
    ...
    PRIV_REQUIRES bt  # Asegurar que está incluido
)
```

### 9.5 Crash al llamar `ble_gap_disc()`

**Síntomas**:
```
Guru Meditation Error: Core  0 panic'ed (LoadProhibited)
```

**Causas**:
- NimBLE no sincronizado aún
- `g_own_addr_type` no inicializado

**Soluciones**:
1. Asegurar que `ble_client_on_sync()` se ejecutó antes de escanear
2. Solo llamar a funciones BLE después de que el stack esté sincronizado
3. Agregar flag de estado:
   ```c
   static bool g_ble_ready = false;

   static void ble_client_on_sync(void) {
       // ... código existente
       g_ble_ready = true;
   }

   void ble_client_start_scan(...) {
       if (!g_ble_ready) {
           ESP_LOGW(TAG, "BLE not ready yet");
           return;
       }
       // ... resto del código
   }
   ```

---

## 10. Checklist de Migración

### ✅ Preparación

- [ ] Crear proyecto nuevo para ESP32-S3
- [ ] Configurar target: `idf.py set-target esp32s3`
- [ ] Copiar archivos necesarios desde proyecto P4

### ✅ Configuración

- [ ] Crear `idf_component.yml` con dependencia `espressif/nimble`
- [ ] Crear `sdkconfig.defaults` con configuración BLE
- [ ] Actualizar `CMakeLists.txt` (eliminar `esp_hosted`)
- [ ] Verificar `CONFIG_BT_CONTROLLER_ENABLED=y`

### ✅ Código

- [ ] Copiar `ble_client.h` sin cambios
- [ ] Copiar `ble_client.c` y modificar `ble_client_init()`:
  - [ ] Agregar `#include "esp_nimble_hci.h"`
  - [ ] Agregar `esp_nimble_hci_init()` antes de `nimble_port_init()`
  - [ ] Eliminar `#include "esp_hosted.h"`
- [ ] Copiar `treadmill_state.h` y `treadmill_state.c`
- [ ] Actualizar `main.c`:
  - [ ] Eliminar `esp_hosted_init()`
  - [ ] Mantener `nvs_flash_init()`
  - [ ] Llamar `ble_client_init()`

### ✅ Testing

- [ ] Compilar sin errores: `idf.py build`
- [ ] Flashear: `idf.py flash`
- [ ] Verificar logs de inicialización
- [ ] Probar escaneo de dispositivos
- [ ] Probar conexión a monitor HR
- [ ] Verificar recepción de BPM
- [ ] Probar persistencia NVS
- [ ] Probar reconexión automática

---

## 11. Resumen de Diferencias Clave

| Aspecto | ESP32-P4 (Original) | ESP32-S3 (Destino) |
|---------|---------------------|-------------------|
| **Radio BLE** | Externa (ESP32-C6) | Integrada |
| **Transporte** | ESP-Hosted (SDIO) | N/A |
| **Inicialización** | `esp_hosted_init()` + NimBLE | Solo NimBLE + `esp_nimble_hci_init()` |
| **Configuración** | `CONFIG_BT_CONTROLLER_DISABLED=y` | `CONFIG_BT_CONTROLLER_ENABLED=y` |
| **Dependencias** | `esp_hosted`, `esp_wifi_remote` | Solo `bt` |
| **Complejidad** | Alta (2 chips) | Baja (1 chip) |
| **Código BLE** | Igual | Igual (solo cambia init) |

---

## 12. Referencias

- **NimBLE Documentación**: https://github.com/apache/mynewt-nimble
- **ESP-IDF BLE**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/bluetooth/nimble/index.html
- **Heart Rate Profile**: https://www.bluetooth.com/specifications/specs/heart-rate-profile-1-0/
- **Proyecto Original**: `/home/user/Treadmill/Consola/`

---

## Anexo A: Código Fuente Completo de Referencia

### Ubicación en Proyecto Original

```
/home/user/Treadmill/Consola/
├── docs/BLE.md                    # Documentación original
├── main/
│   ├── ble_client.c               # Implementación principal
│   ├── ble_client.h               # API pública
│   ├── treadmill_state.h          # Estado global
│   └── treadmill_state.c          # Implementación de estado
└── idf_component.yml              # Dependencias
```

### Archivos Clave del Proyecto Original

- `Consola/main/ble_client.c` (líneas 1-535)
- `Consola/main/ble_client.h` (líneas 1-47)
- `Consola/main/main.c` (líneas 92-92: llamada a `ble_client_init()`)
- `Consola/docs/BLE.md` (documentación completa)

---

## Anexo B: Ejemplo Mínimo Funcional

Para un proyecto de prueba rápido que solo muestre BPM en consola:

```c
// main.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ble_client.h"
#include "treadmill_state.h"

void app_main(void) {
    // Init NVS
    nvs_flash_init();

    // Create mutex
    g_state_mutex = xSemaphoreCreateMutex();

    // Init BLE
    ble_client_init();

    // Monitor BPM
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            printf("BLE: %s, BPM: %d\n",
                   g_treadmill_state.ble_connected ? "Connected" : "Disconnected",
                   g_treadmill_state.real_pulse);
            xSemaphoreGive(g_state_mutex);
        }
    }
}
```

---

**Fin del Informe**

Este documento proporciona toda la información necesaria para replicar la funcionalidad BLE del proyecto Treadmill Console (ESP32-P4) en un ESP32-S3. Las principales ventajas de la migración son la simplificación de la arquitectura (eliminando la necesidad de ESP-Hosted) y la reducción de costos (un solo chip en lugar de dos).
