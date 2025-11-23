# Informe: Replicación de Lógica WiFi en ESP32-S3

**Proyecto Origen**: Treadmill Console (ESP32-P4)
**Proyecto Destino**: ESP32-S3
**Fecha**: 2025-11-23
**Objetivo**: Replicar toda la funcionalidad WiFi de gestión de redes, conexión automática y comunicación HTTP

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

### ¿Qué hace el sistema WiFi actual?

El proyecto actual implementa un **sistema completo de WiFi** que:
- Escanea redes WiFi disponibles
- Almacena credenciales en NVS
- Conexión automática a redes guardadas (con prioridad por uso reciente)
- Verifica conectividad a Internet
- Sube datos de entrenamiento via HTTP POST a Google Sheets
- Sincroniza hora con servidores NTP
- Descarga archivos via HTTP/HTTPS

### Arquitectura Actual (ESP32-P4)

```
ESP32-P4 (sin WiFi) ←→ ESP-Hosted (SDIO) ←→ ESP32-C6 (con WiFi) ←→ Router
    ↑
WiFi Manager + WiFi Client
```

### Arquitectura Objetivo (ESP32-S3)

```
ESP32-S3 (con WiFi integrado) ←→ Router
    ↑
WiFi Manager + WiFi Client
```

**Ventaja**: El ESP32-S3 tiene radio WiFi integrado, por lo que NO necesita ESP-Hosted. La implementación será más simple y directa.

---

## 2. Diferencias Arquitectónicas Clave

### ESP32-P4 (Proyecto Actual)

| Característica | Detalle |
|----------------|---------|
| **Radio WiFi** | ❌ No tiene |
| **Solución** | ESP-Hosted con ESP32-C6 externo |
| **Transporte** | SDIO |
| **Stack WiFi** | Remoto (en C6) |
| **Complejidad** | Alta (2 chips) |

### ESP32-S3 (Proyecto Destino)

| Característica | Detalle |
|----------------|---------|
| **Radio WiFi** | ✅ Integrado (802.11 b/g/n) |
| **Solución** | Nativo |
| **Transporte** | N/A |
| **Stack WiFi** | Local |
| **Complejidad** | Baja (1 chip) |

### Cambios Requeridos

1. ❌ **Eliminar**: `esp_hosted_init()` y dependencia de `esp_hosted`
2. ❌ **Eliminar**: Dependencia de `esp_wifi_remote`
3. ✅ **Mantener**: Todo el código de `wifi_manager.c` y `wifi_client.c`
4. ✅ **Simplificar**: Inicialización (sin capa de transporte)

---

## 3. Componentes y Dependencias

### 3.1 Dependencias IDF Component Manager

**Archivo**: `idf_component.yml` (en el directorio raíz del proyecto)

```yaml
dependencies:
  # NO incluir esp_hosted ni esp_wifi_remote para ESP32-S3
  # WiFi es nativo en ESP32-S3
```

### 3.2 Componentes ESP-IDF Requeridos

**Archivo**: `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.c"
         "wifi_manager.c"
         "wifi_client.c"
         # ... otros archivos
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_wifi          # WiFi nativo
        esp_netif         # TCP/IP stack
        esp_http_client   # Cliente HTTP
        esp_http_server   # Servidor HTTP (opcional)
        # NO incluir esp_hosted ni esp_wifi_remote
)
```

### 3.3 Configuración Kconfig (sdkconfig)

**Archivo**: `sdkconfig.defaults` (crear si no existe)

```ini
# ===== WiFi Configuration =====
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP_WIFI_TX_BUFFER_TYPE=1
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32
CONFIG_ESP_WIFI_AMPDU_TX_ENABLED=y
CONFIG_ESP_WIFI_TX_BA_WIN=6
CONFIG_ESP_WIFI_AMPDU_RX_ENABLED=y
CONFIG_ESP_WIFI_RX_BA_WIN=6
CONFIG_ESP_WIFI_NVS_ENABLED=y
CONFIG_ESP_WIFI_IRAM_OPT=y
CONFIG_ESP_WIFI_RX_IRAM_OPT=y

# WPA3 Support
CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y
CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT=y

# Power Management
CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=y

# ===== TCP/IP Configuration =====
CONFIG_LWIP_MAX_SOCKETS=10
CONFIG_LWIP_SO_REUSE=y
CONFIG_LWIP_SO_RCVBUF=y

# ===== HTTP Client Configuration =====
CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y
CONFIG_ESP_TLS_INSECURE=n
CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=n

# ===== SNTP Configuration =====
CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000

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
│   ├── wifi_manager.c          # Copiado SIN CAMBIOS
│   ├── wifi_manager.h          # Copiado SIN CAMBIOS
│   ├── wifi_client.c           # Copiado SIN CAMBIOS (casi)
│   └── wifi_client.h           # Copiado SIN CAMBIOS
├── idf_component.yml           # Nuevo (sin esp_hosted)
├── sdkconfig.defaults          # Nuevo (config WiFi)
└── CMakeLists.txt              # Estándar ESP-IDF
```

---

## 5. Configuración del Proyecto

### 5.1 CMakeLists.txt Principal

**Archivo**: `CMakeLists.txt` (raíz del proyecto)

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_wifi_project)
```

### 5.2 idf_component.yml

**Archivo**: `idf_component.yml`

```yaml
## IDF Component Manager Manifest File
dependencies:
  # WiFi es nativo en ESP32-S3, no se necesitan dependencias externas
```

### 5.3 Configuración menuconfig

Después de copiar `sdkconfig.defaults`, ejecutar:

```bash
idf.py set-target esp32s3
idf.py menuconfig
```

Verificar en `Component config → Wi-Fi`:
- ✅ WiFi enabled
- ✅ Station mode support
- ✅ WPA3 support (opcional pero recomendado)

---

## 6. Implementación Detallada

### 6.1 Módulo: `wifi_manager.h` y `wifi_manager.c`

**Modificaciones**: ❌ **NINGUNA** (copiar tal cual)

Este módulo es **completamente independiente de ESP-Hosted** y funciona igual en ESP32-S3.

#### API Principal

```c
// Inicialización
esp_err_t wifi_manager_init(void);

// Escaneo de redes
esp_err_t wifi_manager_scan_networks(wifi_network_info_t *networks,
                                     uint16_t max_networks,
                                     uint16_t *num_found);

// Credenciales
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_load_credentials(const char *ssid, char *password);
bool wifi_manager_has_credentials(const char *ssid);
esp_err_t wifi_manager_delete_credentials(const char *ssid);

// Gestión de prioridad (última red usada)
esp_err_t wifi_manager_set_last_connected(const char *ssid);
esp_err_t wifi_manager_get_saved_ssids_ordered(wifi_network_info_t *networks,
                                               uint16_t max_networks,
                                               uint16_t *num_found);
```

#### Estructura de Datos

```c
typedef struct {
    char ssid[WIFI_MANAGER_MAX_SSID_LEN];
    int8_t rssi;
    wifi_auth_mode_t auth_mode;
} wifi_network_info_t;
```

#### Almacenamiento NVS

**Namespace**: `"wifi_creds"`

**Formato**:
- Contraseña: Clave = SSID, Valor = password
- Orden de prioridad: Clave = `"ssid_order"`, Valor = `"SSID1,SSID2,SSID3"`

**Ejemplo**:
```
Namespace: wifi_creds
├── MyHomeWiFi = "password123"
├── OfficeWiFi = "office456"
└── ssid_order = "MyHomeWiFi,OfficeWiFi"
```

---

### 6.2 Módulo: `wifi_client.h` y `wifi_client.c`

**Modificaciones**: ⚠️ **Solo en inicialización** (eliminar `esp_hosted_init`)

#### API Principal

```c
// Inicialización
esp_err_t wifi_client_init(void);

// Conexión
esp_err_t wifi_client_connect(const char *ssid, const char *password);

// Estado
bool is_wifi_connected(void);
bool is_internet_connected(void);
void check_internet_connectivity(void);

// Descarga HTTP
void wifi_download_file(const char *url);
extern char *g_downloaded_file_content;
extern int g_downloaded_file_size;

// Subida de datos (adaptable según tu backend)
void wifi_upload_training_data(const char *training_data, const char *training_type);
```

#### Código Completo Modificado para ESP32-S3

**Archivo**: `main/wifi_client.c`

```c
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "wifi_client.h"
#include "wifi_manager.h"

static const char *TAG = "WIFI_CLIENT";
static const char *TAG_CONNECTIVITY = "WIFI_CONNECTIVITY";

// Event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Variables para conexión iterativa
static wifi_network_info_t s_saved_networks[WIFI_MANAGER_MAX_NETWORKS];
static uint16_t s_num_saved_networks = 0;
static int s_connection_attempt_index = 0;

// Estado WiFi
static bool g_wifi_connected = false;
bool g_internet_connected = false;

// SNTP
static bool g_sntp_initialized = false;

// Descarga HTTP
char *g_downloaded_file_content = NULL;
int g_downloaded_file_size = 0;
static int received_len = 0;
SemaphoreHandle_t g_download_mutex = NULL;

// Forward declarations
static void wifi_connect_task(void *pvParameters);
static void internet_check_task(void *pvParameters);
static void try_next_saved_network(void);
static bool sync_time_sntp(void);

// ==================== VERIFICACIÓN DE CONECTIVIDAD ====================

esp_err_t _http_connectivity_event_handler(esp_http_client_event_t *evt)
{
    // Silencioso para no saturar logs
    return ESP_OK;
}

void check_internet_connectivity(void)
{
    esp_http_client_config_t config = {
        .url = "http://connectivitycheck.gstatic.com/generate_204",
        .event_handler = _http_connectivity_event_handler,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 204) {
            ESP_LOGI(TAG_CONNECTIVITY, "Internet connectivity confirmed.");
            g_internet_connected = true;
        } else {
            ESP_LOGW(TAG_CONNECTIVITY, "Internet check failed with status code: %d", status_code);
            g_internet_connected = false;
        }
    } else {
        ESP_LOGE(TAG_CONNECTIVITY, "Internet check failed: %s", esp_err_to_name(err));
        g_internet_connected = false;
    }
    esp_http_client_cleanup(client);
}

bool is_internet_connected(void) {
    return g_internet_connected;
}

bool is_wifi_connected(void) {
    return g_wifi_connected;
}

static void internet_check_task(void *pvParameters) {
    check_internet_connectivity();

    // Sincronizar hora si internet está disponible
    if (g_internet_connected) {
        sync_time_sntp();
    }

    vTaskDelete(NULL);
}

// ==================== CONEXIÓN AUTOMÁTICA ====================

static void try_next_saved_network(void) {
    if (s_connection_attempt_index < s_num_saved_networks) {
        ESP_LOGI(TAG, "Attempting to connect to %s (%d/%d)",
                 s_saved_networks[s_connection_attempt_index].ssid,
                 s_connection_attempt_index + 1, s_num_saved_networks);

        wifi_config_t wifi_config = {0};
        strlcpy((char *)wifi_config.sta.ssid,
                s_saved_networks[s_connection_attempt_index].ssid,
                sizeof(wifi_config.sta.ssid));

        char password[WIFI_MANAGER_MAX_PASSWORD_LEN];
        esp_err_t err = wifi_manager_load_credentials((const char*)wifi_config.sta.ssid, password);

        s_connection_attempt_index++;

        if (err == ESP_OK) {
            strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Failed to load password for %s. Skipping.",
                     (char*)wifi_config.sta.ssid);
            try_next_saved_network();
        }
    } else {
        ESP_LOGI(TAG, "No more saved networks to try.");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

        // Aquí podrías abrir una pantalla de selección de WiFi en UI
        // ui_open_wifi_list();
    }
}

// ==================== EVENT HANDLER ====================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START: Initializing connection process.");
        wifi_manager_get_saved_ssids_ordered(s_saved_networks,
                                             WIFI_MANAGER_MAX_NETWORKS,
                                             &s_num_saved_networks);
        s_connection_attempt_index = 0;
        try_next_saved_network();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED: Connection failed or lost.");
        g_wifi_connected = false;
        g_internet_connected = false;

        ESP_LOGI(TAG, "Trying next saved network...");
        try_next_saved_network();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        g_wifi_connected = true;

        // Reset connection attempts
        s_connection_attempt_index = 0;

        // Actualizar prioridad en NVS
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            wifi_manager_set_last_connected((const char*)ap_info.ssid);
        }

        // Configurar DNS (Google DNS)
        esp_netif_dns_info_t dns_info;
        IP_ADDR4(&dns_info.ip, 8, 8, 8, 8);
        esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        ESP_LOGI(TAG, "Manually set DNS server to 8.8.8.8");

        // Verificar conectividad a Internet en tarea separada
        xTaskCreate(&internet_check_task, "internet_check_task", 4096, NULL, 5, NULL);
    }
}

// ==================== SINCRONIZACIÓN SNTP ====================

static bool sync_time_sntp(void)
{
    if (!g_sntp_initialized) {
        ESP_LOGI(TAG, "Initializing SNTP...");
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        g_sntp_initialized = true;
    }

    // Verificar si ya está sincronizado
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year + 1900 >= 2024) {
        ESP_LOGI(TAG, "Time already synchronized: %d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }

    // Esperar sincronización (máximo 10 segundos)
    ESP_LOGI(TAG, "Waiting for SNTP time sync...");
    int retry = 0;
    const int retry_count = 10;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGD(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year + 1900 >= 2024) {
        // Configurar zona horaria (Europa/Madrid UTC+1)
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        time(&now);
        localtime_r(&now, &timeinfo);

        ESP_LOGI(TAG, "SNTP time synced successfully: %d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    } else {
        ESP_LOGW(TAG, "Failed to sync time via SNTP");
        return false;
    }
}

// ==================== INICIALIZACIÓN ====================

static void wifi_connect_task(void *pvParameters)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "wifi_connect_task: WiFi Connected");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "wifi_connect_task: WiFi Connection Failed");
    } else {
        ESP_LOGE(TAG, "wifi_connect_task: UNEXPECTED EVENT");
    }
    vTaskDelete(NULL);
}

esp_err_t wifi_client_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Client for ESP32-S3...");

    // Crear mutex para descarga
    if (g_download_mutex == NULL) {
        g_download_mutex = xSemaphoreCreateMutex();
        if (g_download_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create download mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_wifi_event_group = xEventGroupCreate();

    // *** INICIALIZACIÓN NATIVA PARA ESP32-S3 ***
    // No se necesita esp_hosted_init()
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registrar event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi client initialization finished.");

    // Crear tarea de conexión
    xTaskCreate(&wifi_connect_task, "wifi_connect_task", 4096, NULL, 5, NULL);

    return ESP_OK;
}

// ==================== CONEXIÓN MANUAL ====================

esp_err_t wifi_client_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    return esp_wifi_connect();
}

// ==================== DESCARGA HTTP ====================

esp_err_t _http_download_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (g_downloaded_file_content) {
                    free(g_downloaded_file_content);
                    g_downloaded_file_content = NULL;
                }
                g_downloaded_file_size = 0;
                received_len = 0;
                xSemaphoreGive(g_download_mutex);
            }
            break;

        case HTTP_EVENT_ON_HEADER:
            if (g_downloaded_file_size == 0 && strcasecmp(evt->header_key, "Content-Length") == 0) {
                int content_length = atoi(evt->header_value);
                if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    g_downloaded_file_size = content_length;
                    if (g_downloaded_file_size > 0) {
                        g_downloaded_file_content = (char *) malloc(g_downloaded_file_size + 1);
                        if (!g_downloaded_file_content) {
                            ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
                            xSemaphoreGive(g_download_mutex);
                            return ESP_FAIL;
                        }
                    }
                    xSemaphoreGive(g_download_mutex);
                }
            }
            break;

        case HTTP_EVENT_ON_DATA:
            if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (g_downloaded_file_content) {
                    memcpy(g_downloaded_file_content + received_len, evt->data, evt->data_len);
                    received_len += evt->data_len;
                }
                xSemaphoreGive(g_download_mutex);
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (g_downloaded_file_content) {
                    g_downloaded_file_content[received_len] = '\0';
                    g_downloaded_file_size = received_len;
                    ESP_LOGI(TAG, "Download complete. Total received: %d bytes", received_len);
                }
                xSemaphoreGive(g_download_mutex);
            }
            break;

        default:
            break;
    }
    return ESP_OK;
}

void wifi_download_file(const char *url)
{
    if (!g_wifi_connected) {
        ESP_LOGW(TAG, "WiFi not connected, cannot download file");
        return;
    }

    ESP_LOGI(TAG, "Starting HTTP download from: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_download_event_handler,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                 status_code, g_downloaded_file_size);
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// ==================== SUBIDA DE DATOS ====================

void wifi_upload_training_data(const char *training_data, const char *training_type)
{
    if (!g_wifi_connected || !g_internet_connected) {
        ESP_LOGW(TAG, "WiFi or Internet not connected, cannot upload data");
        return;
    }

    ESP_LOGI(TAG, "Uploading training data for type: %s", training_type);

    // Ejemplo: POST a Google Sheets (adaptar según tu backend)
    const char *url = NULL;
    if (strcmp(training_type, "ina") == 0) {
        url = "https://script.google.com/macros/s/YOUR_SCRIPT_ID_INA/exec";
    } else if (strcmp(training_type, "itsaso") == 0) {
        url = "https://script.google.com/macros/s/YOUR_SCRIPT_ID_ITSASO/exec";
    } else {
        ESP_LOGW(TAG, "Unknown training type: %s", training_type);
        return;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_post_field(client, training_data, strlen(training_data));
    esp_http_client_set_header(client, "Content-Type", "text/plain");

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status_code);

        if (status_code >= 200 && status_code < 300) {
            ESP_LOGI(TAG, "Upload successful!");
        } else {
            ESP_LOGW(TAG, "Upload failed with status code: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
```

---

### 6.3 Archivo: `main.c` (Versión ESP32-S3)

**Ubicación**: `main/main.c`

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "wifi_client.h"

static const char *TAG = "MainApp";

void app_main(void) {
    // Inicializar NVS (requerido para WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // *** NO NECESARIO PARA ESP32-S3: esp_hosted_init() ***
    // El ESP32-S3 tiene WiFi integrado

    // Inicializar WiFi Manager
    ESP_LOGI(TAG, "Initializing WiFi Manager...");
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "wifi_manager_init() failed: %s", esp_err_to_name(ret));
    }

    // Inicializar WiFi Client (conecta automáticamente)
    ESP_LOGI(TAG, "Initializing WiFi Client...");
    ret = wifi_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_client_init() failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Initialization complete.");

    // Ejemplo: Tarea de monitoreo
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (is_wifi_connected()) {
            if (is_internet_connected()) {
                ESP_LOGI(TAG, "Status: WiFi ✓ | Internet ✓");
            } else {
                ESP_LOGI(TAG, "Status: WiFi ✓ | Internet ✗");
            }
        } else {
            ESP_LOGI(TAG, "Status: WiFi ✗ | Internet ✗");
        }
    }
}
```

---

## 7. Integración con el Sistema

### 7.1 Flujo de Inicialización

```
app_main()
   │
   ├─→ nvs_flash_init()
   │
   ├─→ wifi_manager_init()
   │    └─→ Abrir namespace NVS "wifi_creds"
   │
   └─→ wifi_client_init()
        │
        ├─→ esp_netif_init()          // TCP/IP stack
        ├─→ esp_event_loop_create_default()
        ├─→ esp_netif_create_default_wifi_sta()
        ├─→ esp_wifi_init()           // Inicializar WiFi nativo
        ├─→ registrar event handlers
        ├─→ esp_wifi_set_mode(WIFI_MODE_STA)
        └─→ esp_wifi_start()
             │
             └─→ WIFI_EVENT_STA_START
                  │
                  ├─→ Cargar redes guardadas (ordenadas por prioridad)
                  └─→ Intentar conectar automáticamente
```

### 7.2 Uso desde la Aplicación

#### Ejemplo 1: Escanear Redes desde UI

```c
void on_wifi_scan_button_pressed(void) {
    wifi_network_info_t networks[20];
    uint16_t num_found;

    esp_err_t ret = wifi_manager_scan_networks(networks, 20, &num_found);
    if (ret == ESP_OK) {
        for (int i = 0; i < num_found; i++) {
            printf("SSID: %s, RSSI: %d dBm, Auth: %d\n",
                   networks[i].ssid, networks[i].rssi, networks[i].auth_mode);

            // Agregar a lista en UI
            ui_add_network_to_list(networks[i].ssid, networks[i].rssi);
        }
    }
}
```

#### Ejemplo 2: Conectar a Red Seleccionada

```c
void on_network_selected(const char *ssid) {
    // Verificar si ya tenemos credenciales guardadas
    if (wifi_manager_has_credentials(ssid)) {
        // Conectar automáticamente (la password se carga internamente)
        char password[WIFI_MANAGER_MAX_PASSWORD_LEN];
        wifi_manager_load_credentials(ssid, password);
        wifi_client_connect(ssid, password);
    } else {
        // Mostrar diálogo para ingresar password
        ui_show_password_dialog(ssid);
    }
}

void on_password_entered(const char *ssid, const char *password) {
    // Guardar credenciales
    wifi_manager_save_credentials(ssid, password);

    // Conectar
    wifi_client_connect(ssid, password);
}
```

#### Ejemplo 3: Subir Datos de Entrenamiento

```c
void on_training_finished(void) {
    // Preparar datos en formato CSV
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

    char data[512];
    snprintf(data, sizeof(data),
             "%s,%lu,%.2f,%.1f,%.1f,%.1f,%d,%.0f",
             timestamp,           // Timestamp
             elapsed_seconds,     // Duración
             distance_km,         // Distancia
             avg_speed_kmh,       // Velocidad promedio
             max_speed_kmh,       // Velocidad máxima
             avg_incline_percent, // Inclinación promedio
             avg_bpm,             // Pulsaciones promedio
             calories);           // Calorías

    // Subir datos
    if (is_internet_connected()) {
        wifi_upload_training_data(data, "ina");
    } else {
        ESP_LOGW("APP", "Cannot upload: No internet connection");
        // Guardar localmente para subir después
    }
}
```

#### Ejemplo 4: Descargar Archivo

```c
void on_download_button_pressed(void) {
    if (!is_internet_connected()) {
        ESP_LOGW("APP", "Cannot download: No internet connection");
        return;
    }

    // Iniciar descarga
    wifi_download_file("http://example.com/training_plan.txt");

    // Esperar a que complete (o usar callback)
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Leer contenido descargado
    if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (g_downloaded_file_content != NULL) {
            printf("Downloaded content:\n%s\n", g_downloaded_file_content);

            // Procesar contenido
            parse_training_plan(g_downloaded_file_content);
        }
        xSemaphoreGive(g_download_mutex);
    }
}
```

---

## 8. Testing y Validación

### 8.1 Prueba Básica: Logs de Inicialización

Después de compilar y flashear:

```
I (xxxx) MainApp: Initializing WiFi Manager...
I (xxxx) WIFI_MANAGER: Initializing WiFi Manager
I (xxxx) WIFI_MANAGER: WiFi Manager initialized successfully
I (xxxx) MainApp: Initializing WiFi Client...
I (xxxx) WIFI_CLIENT: Initializing WiFi Client for ESP32-S3...
I (xxxx) WIFI_CLIENT: WiFi client initialization finished.
I (xxxx) WIFI_CLIENT: WIFI_EVENT_STA_START: Initializing connection process.
I (xxxx) WIFI_MANAGER: Found 2 saved SSIDs in order
I (xxxx) WIFI_CLIENT: Attempting to connect to MyHomeWiFi (1/2)
I (xxxx) WIFI_CLIENT: Got IP address: 192.168.1.100
I (xxxx) WIFI_CONNECTIVITY: Internet connectivity confirmed.
I (xxxx) WIFI_CLIENT: SNTP time synced successfully: 2025-11-23 14:30:00
```

### 8.2 Prueba de Escaneo

```c
void test_scan(void) {
    wifi_network_info_t networks[20];
    uint16_t num_found;

    esp_err_t ret = wifi_manager_scan_networks(networks, 20, &num_found);
    if (ret == ESP_OK) {
        ESP_LOGI("TEST", "Found %d networks:", num_found);
        for (int i = 0; i < num_found; i++) {
            ESP_LOGI("TEST", "  %d. %s (%d dBm)",
                     i+1, networks[i].ssid, networks[i].rssi);
        }
    }
}
```

Logs esperados:

```
I (xxxx) WIFI_MANAGER: Starting WiFi scan...
I (xxxx) WIFI_MANAGER: Found 5 access points
I (xxxx) WIFI_MANAGER: Returning 5 networks
I (xxxx) TEST: Found 5 networks:
I (xxxx) TEST:   1. MyHomeWiFi (-45 dBm)
I (xxxx) TEST:   2. OfficeNetwork (-60 dBm)
I (xxxx) TEST:   3. Neighbor_WiFi (-75 dBm)
```

### 8.3 Prueba de Conexión Manual

```c
void test_connect(void) {
    const char *ssid = "TestNetwork";
    const char *password = "testpass123";

    // Guardar credenciales
    wifi_manager_save_credentials(ssid, password);

    // Conectar
    wifi_client_connect(ssid, password);

    // Esperar conexión
    vTaskDelay(pdMS_TO_TICKS(10000));

    if (is_wifi_connected()) {
        ESP_LOGI("TEST", "Connected successfully!");

        if (is_internet_connected()) {
            ESP_LOGI("TEST", "Internet access confirmed!");
        }
    }
}
```

### 8.4 Prueba de Subida HTTP

```c
void test_upload(void) {
    const char *test_data = "2025-11-23 14:30:00,1800,5.2,10.5,15.0,5.0,145,350";

    wifi_upload_training_data(test_data, "ina");

    // Verificar logs del servidor
}
```

---

## 9. Troubleshooting

### 9.1 WiFi no se conecta

**Síntomas**:
```
W (xxxx) WIFI_CLIENT: Failed to connect to SSID: MyNetwork
```

**Soluciones**:
1. Verificar password en NVS:
   ```c
   char password[64];
   esp_err_t ret = wifi_manager_load_credentials("MyNetwork", password);
   ESP_LOGI("DEBUG", "Password: %s, ret=%d", password, ret);
   ```
2. Verificar que el router es 2.4 GHz (no 5 GHz)
3. Verificar alcance de señal (RSSI > -80 dBm)
4. Intentar conexión manual:
   ```c
   wifi_client_connect("MyNetwork", "mypassword");
   ```

### 9.2 Internet no detectado

**Síntomas**:
- `g_wifi_connected = true`
- `g_internet_connected = false`

**Soluciones**:
1. Verificar que el router tiene salida a Internet
2. Probar ping manual:
   ```bash
   ping 8.8.8.8
   ```
3. Verificar DNS:
   ```c
   // Ya configurado en código con 8.8.8.8
   ```
4. Probar URL alternativa:
   ```c
   // Cambiar en check_internet_connectivity()
   .url = "http://www.google.com"
   ```

### 9.3 Error de compilación: "undefined reference to esp_wifi_init"

**Causa**:
- Falta componente `esp_wifi` en `CMakeLists.txt`

**Solución**:
```cmake
idf_component_register(
    ...
    PRIV_REQUIRES esp_wifi esp_netif
)
```

### 9.4 Hora SNTP no sincroniza

**Síntomas**:
- Timestamp con año 1970
- `sync_time_sntp()` retorna false

**Soluciones**:
1. Verificar que hay Internet:
   ```c
   if (!is_internet_connected()) {
       ESP_LOGW(TAG, "No Internet, cannot sync time");
   }
   ```
2. Probar servidor NTP alternativo:
   ```c
   esp_sntp_setservername(0, "time.google.com");
   ```
3. Aumentar timeout:
   ```c
   const int retry_count = 20; // En lugar de 10
   ```

### 9.5 Descarga HTTP falla con error -1

**Síntomas**:
```
E (xxxx) WIFI_CLIENT: HTTP GET request failed: ESP_FAIL
```

**Soluciones**:
1. Verificar URL:
   ```c
   ESP_LOGI("DEBUG", "Downloading from: %s", url);
   ```
2. Probar con HTTP en lugar de HTTPS (para depurar)
3. Aumentar timeout:
   ```c
   .timeout_ms = 30000, // 30 segundos
   ```
4. Verificar memoria disponible:
   ```c
   ESP_LOGI("DEBUG", "Free heap: %d bytes", esp_get_free_heap_size());
   ```

---

## 10. Checklist de Migración

### ✅ Preparación

- [ ] Crear proyecto nuevo para ESP32-S3
- [ ] Configurar target: `idf.py set-target esp32s3`
- [ ] Copiar archivos necesarios desde proyecto P4

### ✅ Configuración

- [ ] Actualizar `idf_component.yml` (eliminar `esp_hosted`)
- [ ] Crear `sdkconfig.defaults` con configuración WiFi
- [ ] Actualizar `CMakeLists.txt` (eliminar `esp_hosted`, `esp_wifi_remote`)
- [ ] Verificar configuración en menuconfig

### ✅ Código

- [ ] Copiar `wifi_manager.h` sin cambios
- [ ] Copiar `wifi_manager.c` sin cambios
- [ ] Copiar `wifi_client.h` sin cambios
- [ ] Copiar `wifi_client.c` y modificar `wifi_client_init()`:
  - [ ] Eliminar referencias a `esp_hosted`
  - [ ] Verificar que usa `esp_wifi_init()` nativo
- [ ] Actualizar `main.c`:
  - [ ] Eliminar `esp_hosted_init()`
  - [ ] Mantener `nvs_flash_init()`
  - [ ] Llamar `wifi_manager_init()`
  - [ ] Llamar `wifi_client_init()`

### ✅ Testing

- [ ] Compilar sin errores: `idf.py build`
- [ ] Flashear: `idf.py flash`
- [ ] Verificar logs de inicialización
- [ ] Probar escaneo de redes
- [ ] Probar conexión automática
- [ ] Verificar conectividad a Internet
- [ ] Probar sincronización SNTP
- [ ] Probar descarga HTTP
- [ ] Probar subida HTTP POST

---

## 11. Resumen de Diferencias Clave

| Aspecto | ESP32-P4 (Original) | ESP32-S3 (Destino) |
|---------|---------------------|-------------------|
| **Radio WiFi** | Externa (ESP32-C6) | Integrada |
| **Transporte** | ESP-Hosted (SDIO) | N/A |
| **Inicialización** | `esp_hosted_init()` + WiFi | Solo WiFi nativo |
| **Dependencias** | `esp_hosted`, `esp_wifi_remote` | Solo `esp_wifi`, `esp_netif` |
| **Código Manager** | Idéntico | Idéntico |
| **Código Client** | Con ESP-Hosted | Sin ESP-Hosted |
| **Complejidad** | Alta (2 chips) | Baja (1 chip) |

---

## 12. Referencias

- **ESP-IDF WiFi API**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/network/esp_wifi.html
- **ESP-IDF HTTP Client**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/esp_http_client.html
- **ESP-IDF SNTP**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/esp_sntp.html
- **ESP-Hosted (referencia)**: https://github.com/espressif/esp-hosted
- **Proyecto Original**: `/home/user/Treadmill/Consola/`

---

## Anexo A: Código Fuente Completo de Referencia

### Ubicación en Proyecto Original

```
/home/user/Treadmill/Consola/
├── docs/WIFI.md                   # Documentación original
├── main/
│   ├── wifi_manager.c             # Gestión de credenciales (copiar sin cambios)
│   ├── wifi_manager.h             # API manager (copiar sin cambios)
│   ├── wifi_client.c              # Implementación cliente (modificar init)
│   └── wifi_client.h              # API cliente (copiar sin cambios)
└── idf_component.yml              # Dependencias
```

### Archivos Clave del Proyecto Original

- `Consola/main/wifi_manager.c` (líneas 1-384) - Sin modificaciones
- `Consola/main/wifi_manager.h` (líneas 1-111) - Sin modificaciones
- `Consola/main/wifi_client.c` (líneas 1-400+) - Solo cambio en `wifi_client_init()`
- `Consola/main/wifi_client.h` (líneas 1-127) - Sin modificaciones
- `Consola/main/main.c` (líneas 42-54) - Eliminar `esp_hosted_init()`
- `Consola/docs/WIFI.md` - Documentación completa

---

## Anexo B: Ejemplo Mínimo Funcional

Para un proyecto de prueba rápido que conecte a WiFi y muestre IP:

```c
// main.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "wifi_client.h"

void app_main(void) {
    // Init NVS
    nvs_flash_init();

    // Init WiFi Manager
    wifi_manager_init();

    // Guardar red de prueba
    wifi_manager_save_credentials("TestNetwork", "testpassword");

    // Init WiFi Client (conecta automáticamente)
    wifi_client_init();

    // Monitor status
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (is_wifi_connected()) {
            if (is_internet_connected()) {
                ESP_LOGI("MAIN", "WiFi ✓ | Internet ✓");
            } else {
                ESP_LOGI("MAIN", "WiFi ✓ | Internet ✗");
            }
        } else {
            ESP_LOGI("MAIN", "WiFi ✗");
        }
    }
}
```

---

**Fin del Informe**

Este documento proporciona toda la información necesaria para replicar la funcionalidad WiFi del proyecto Treadmill Console (ESP32-P4) en un ESP32-S3. La principal ventaja es la simplificación de la arquitectura al eliminar la necesidad de ESP-Hosted y reducir a un solo chip.
