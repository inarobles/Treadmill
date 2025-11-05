# Sistema WiFi - ESP-Hosted + HTTP

Documentación completa del sistema WiFi con ESP-Hosted, escaneo de redes, y subida de datos via HTTP.

## Índice

- [Descripción General](#descripción-general)
- [Arquitectura ESP-Hosted](#arquitectura-esp-hosted)
- [Gestión de Redes (wifi_manager)](#gestión-de-redes-wifi_manager)
- [Cliente WiFi (wifi_client)](#cliente-wifi-wifi_client)
- [Conexión Automática](#conexión-automática)
- [Subida de Entrenamientos](#subida-de-entrenamientos)
- [Sincronización de Hora (SNTP)](#sincronización-de-hora-sntp)
- [API Pública](#api-pública)
- [Troubleshooting](#troubleshooting)

## Descripción General

El sistema WiFi utiliza arquitectura ESP-Hosted con un ESP32-C6 como co-procesador dedicado para RF, liberando al ESP32-P4 de tareas de radio.

**Componentes**:
- `esp_hosted`: Transport layer (SDIO)
- `wifi_manager.c/h`: Gestión de redes y credenciales
- `wifi_client.c/h`: Conexión, HTTP, SNTP

**Características**:
- Escaneo de redes WiFi
- Almacenamiento de credenciales en NVS
- Conexión automática a redes guardadas
- Subida de datos via HTTP/HTTPS
- Sincronización de hora (SNTP)
- Verificación de conectividad a Internet

## Arquitectura ESP-Hosted

### Diagrama de Componentes

```
┌────────────────────────────────────────────────┐
│           ESP32-P4 (Host)                      │
│                                                │
│  ┌─────────────────────────────────────┐      │
│  │  Application Layer                  │      │
│  │  - wifi_manager                     │      │
│  │  - wifi_client                      │      │
│  └──────────────┬──────────────────────┘      │
│                 │                              │
│  ┌──────────────▼──────────────────────┐      │
│  │  ESP-IDF WiFi API (Stub)            │      │
│  │  - esp_wifi_*                       │      │
│  │  - esp_netif_*                      │      │
│  └──────────────┬──────────────────────┘      │
│                 │                              │
│  ┌──────────────▼──────────────────────┐      │
│  │  ESP-Hosted Transport (SDIO)        │      │
│  │  - Serialización de comandos        │      │
│  │  - RPC sobre SDIO                   │      │
│  └──────────────┬──────────────────────┘      │
│                 │                              │
└─────────────────┼──────────────────────────────┘
                  │ SDIO
                  │ (4-bit, ~40 MB/s)
┌─────────────────▼──────────────────────────────┐
│           ESP32-C6 (Slave)                     │
│                                                │
│  ┌─────────────────────────────────────┐      │
│  │  ESP-Hosted Firmware                │      │
│  │  - Command processor                │      │
│  └──────────────┬──────────────────────┘      │
│                 │                              │
│  ┌──────────────▼──────────────────────┐      │
│  │  WiFi Stack (Full)                  │      │
│  │  - 802.11 b/g/n                     │      │
│  │  - WPA2/WPA3                        │      │
│  │  - LWIP TCP/IP Stack                │      │
│  └─────────────────────────────────────┘      │
│                                                │
└────────────────────────────────────────────────┘
```

### Inicialización

```c
// En main.c
esp_err_t ret = esp_hosted_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esp_hosted_init() failed");
}
```

**Firmware del C6**:
- Repositorio: https://github.com/espressif/esp-hosted
- Modo: Slave (co-processor)
- Flash mediante: `idf.py flash` en el proyecto esp-hosted

## Gestión de Redes (wifi_manager)

### Funcionalidades

El módulo `wifi_manager.c/h` proporciona:
- Escaneo de redes disponibles
- Almacenamiento de credenciales en NVS
- Recuperación de credenciales guardadas
- Gestión de prioridad (última red conectada)

### API Principal

**Inicialización**:
```c
esp_err_t wifi_manager_init(void);
```

**Escaneo de Redes**:
```c
wifi_network_info_t networks[WIFI_MANAGER_MAX_NETWORKS];
uint16_t num_found;

esp_err_t ret = wifi_manager_scan_networks(networks,
                                           WIFI_MANAGER_MAX_NETWORKS,
                                           &num_found);
if (ret == ESP_OK) {
    for (int i = 0; i < num_found; i++) {
        printf("SSID: %s, RSSI: %d dBm\n",
               networks[i].ssid, networks[i].rssi);
    }
}
```

**Guardar Credenciales**:
```c
esp_err_t wifi_manager_save_credentials(const char *ssid,
                                        const char *password);
```

**Cargar Credenciales**:
```c
char password[WIFI_MANAGER_MAX_PASSWORD_LEN];
esp_err_t ret = wifi_manager_load_credentials("MyNetwork", password);
if (ret == ESP_OK) {
    // password contiene la contraseña guardada
}
```

**Verificar Existencia**:
```c
bool has_creds = wifi_manager_has_credentials("MyNetwork");
```

**Listar Redes Guardadas** (ordenadas por uso reciente):
```c
wifi_network_info_t saved[WIFI_MANAGER_MAX_NETWORKS];
uint16_t num_saved;

wifi_manager_get_saved_ssids_ordered(saved,
                                     WIFI_MANAGER_MAX_NETWORKS,
                                     &num_saved);
```

### Almacenamiento NVS

**Namespace**: `"wifi_creds"`

**Formato de claves**:
- Contraseña: `"pwd_<SSID>"`
- Última conexión: `"last_conn"`

**Ejemplo de NVS**:
```
Namespace: wifi_creds
├── pwd_MyHomeWiFi = "mypassword123"
├── pwd_OfficeWiFi = "officepass456"
└── last_conn = "MyHomeWiFi"
```

## Cliente WiFi (wifi_client)

### Funcionalidades

El módulo `wifi_client.c/h` proporciona:
- Conexión a redes WiFi
- Verificación de conectividad a Internet
- Descargas HTTP
- Subida de datos via POST
- Sincronización de hora (SNTP)

### Conexión Automática

Al inicializar, el sistema intenta conectarse automáticamente:

```c
// En wifi_client.c
void wifi_client_init(void) {
    // Crea tarea de conexión
    xTaskCreate(wifi_connect_task, "wifi_connect", 8192, NULL, 4, NULL);
}
```

**Lógica de Conexión**:
1. Obtener lista de SSIDs guardadas (ordenadas por uso reciente)
2. Iterar por cada SSID:
   a. Cargar password desde NVS
   b. Intentar conexión
   c. Si falla, pasar a siguiente SSID
3. Si todas fallan, esperar y reintentar

### Estado de Conexión

**Variables Globales**:
```c
extern bool g_wifi_connected;         // WiFi asociado
extern bool g_internet_connected;     // Internet verificado
```

**Verificación de Internet**:
Se ejecuta automáticamente cada 30 segundos mediante `internet_check_task`:
```c
// GET a http://www.google.com
// Si recibe respuesta 200-399 → Internet OK
```

### Subida de Entrenamientos

**Función Principal**:
```c
void wifi_upload_training_data(const char *training_data,
                               const char *training_type);
```

**Destinos de Subida**:
1. **Google Sheets** (via Apps Script):
   - INA: `GOOGLE_SCRIPT_INA`
   - ITSASO: `GOOGLE_SCRIPT_ITSASO`

**Formato de Datos**:
```
timestamp,duration_sec,distance_km,avg_speed,max_speed,avg_incline,avg_bpm,calories
2025-11-05 14:30:00,1800,5.2,10.5,15.0,5.0,145,350
```

**Ejemplo de Uso**:
```c
// En ui.c al finalizar entrenamiento
char data[512];
snprintf(data, sizeof(data),
         "%s,%lu,%.2f,%.1f,%.1f,%.1f,%d,%.0f",
         timestamp, duration, distance, avg_speed,
         max_speed, avg_incline, avg_bpm, calories);

wifi_upload_training_data(data, "ina");
```

**Reintentos**:
- Hasta 3 intentos
- Delay de 2 segundos entre intentos
- Callback de éxito/fallo a UI

### Descargas HTTP (Manual)

**Función**:
```c
void wifi_download_file(void);
```

**Uso**:
```c
// Desde UI o botón
if (g_wifi_connected) {
    wifi_download_file();

    // Esperar 3-4 segundos
    vTaskDelay(pdMS_TO_TICKS(4000));

    // Acceder al contenido
    if (g_downloaded_file_content != NULL) {
        printf("Downloaded: %s\n", g_downloaded_file_content);
    }
}
```

**NOTA**: La descarga es intensiva en CPU y puede causar artefactos visuales temporales debido al bug de cache sync en ESP32-P4. Ver [HTTPS_DOWNLOAD_README.md](../HTTPS_DOWNLOAD_README.md).

## Sincronización de Hora (SNTP)

### Configuración

**Servidor NTP**: `pool.ntp.org`
**Zona Horaria**: UTC+1 (Europa/Madrid)

**Inicialización Automática**:
Cuando WiFi se conecta exitosamente, se sincroniza la hora:
```c
// En wifi_client.c
static bool sync_time_sntp(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Esperar hasta 10 segundos
    // ...

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}
```

### Obtener Timestamp

```c
#include <time.h>

time_t now;
struct tm timeinfo;
char timestamp[64];

time(&now);
localtime_r(&now, &timeinfo);
strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

printf("Timestamp: %s\n", timestamp);  // 2025-11-05 14:30:00
```

## API Pública

### wifi_manager.h

```c
// Inicialización
esp_err_t wifi_manager_init(void);

// Escaneo
esp_err_t wifi_manager_scan_networks(wifi_network_info_t *networks,
                                     uint16_t max_networks,
                                     uint16_t *num_found);

// Credenciales
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_load_credentials(const char *ssid, char *password);
bool wifi_manager_has_credentials(const char *ssid);
esp_err_t wifi_manager_delete_credentials(const char *ssid);

// Estado
esp_err_t wifi_manager_get_current_ssid(char *ssid);
esp_err_t wifi_manager_set_last_connected(const char *ssid);

// Listar guardadas
esp_err_t wifi_manager_get_saved_ssids_ordered(wifi_network_info_t *networks,
                                               uint16_t max_networks,
                                               uint16_t *num_found);
```

### wifi_client.h

```c
// Inicialización
void wifi_client_init(void);

// Descargas
void wifi_download_file(void);
extern char *g_downloaded_file_content;
extern int g_downloaded_file_size;

// Subida
void wifi_upload_training_data(const char *training_data, const char *training_type);

// Estado
extern bool g_wifi_connected;
extern bool g_internet_connected;
```

## Troubleshooting

### WiFi no se conecta

**Síntomas**:
- Log `"Failed to connect to SSID"`
- `g_wifi_connected = false`

**Soluciones**:
1. Verificar firmware ESP-Hosted en C6
2. Confirmar contraseña correcta en NVS
3. Verificar que el router es 2.4 GHz (no 5 GHz)
4. Revisar logs de `WIFI_CLIENT`

**Verificar NVS**:
```c
char password[64];
esp_err_t ret = wifi_manager_load_credentials("MyNetwork", password);
if (ret != ESP_OK) {
    // Credenciales no guardadas o corruptas
}
```

### Internet no detectado

**Síntomas**:
- `g_wifi_connected = true`
- `g_internet_connected = false`

**Causas**:
- Router sin salida a Internet
- Firewall bloqueando HTTP
- Proxy corporativo

**Solución**:
Verificar acceso manual a http://www.google.com desde otra PC en la misma red.

### Subida de datos falla

**Síntomas**:
- Log `"Upload failed after 3 retries"`
- Callback de fallo en UI

**Soluciones**:
1. Verificar que `g_internet_connected = true`
2. Confirmar URLs de Google Scripts activas
3. Revisar formato de datos (CSV correcto)
4. Ver logs de `WIFI_CLIENT` para detalles

**Ejemplo de log de fallo**:
```
E (xxxx) WIFI_CLIENT: HTTP POST failed: 400 Bad Request
```

### Artefactos visuales durante descargas

**Síntomas**:
- Zonas negras temporales en pantalla
- Parpadeos durante HTTPS

**Causa**:
Bug conocido de ESP32-P4 con PSRAM (cache sync errors).

**Solución**:
Ya implementada en código:
- Buffers en SPIRAM sin DMA
- Supresión de logs de cache
- Descargas manuales (no automáticas)

Ver [HTTPS_DOWNLOAD_README.md](../HTTPS_DOWNLOAD_README.md) para detalles.

### Hora incorrecta

**Síntomas**:
- Timestamps con hora UTC en lugar de local
- Hora desincronizada

**Soluciones**:
1. Verificar que SNTP se sincronizó:
   ```c
   time_t now;
   time(&now);
   if (now < 1000000000) {
       // Hora no sincronizada (año < 2001)
   }
   ```

2. Forzar resincronización:
   ```c
   esp_sntp_stop();
   esp_sntp_init();
   ```

3. Verificar zona horaria:
   ```c
   setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
   tzset();
   ```

## Logs y Debugging

### Etiquetas de Log

- `WIFI_CLIENT`: Cliente WiFi general
- `WIFI_CONNECTIVITY`: Estado de conectividad
- `WIFI_DOWNLOADER`: Descargas HTTP

### Mensajes Clave

**Conexión exitosa**:
```
I (xxxx) WIFI_CONNECTIVITY: Connected to AP: MyHomeWiFi
I (xxxx) WIFI_CONNECTIVITY: Got IP: 192.168.1.100
I (xxxx) WIFI_CONNECTIVITY: Internet connection OK
I (xxxx) WIFI_CLIENT: SNTP time synced successfully
```

**Subida exitosa**:
```
I (xxxx) WIFI_CLIENT: Starting upload to Google Sheets...
I (xxxx) WIFI_CLIENT: HTTP POST Status = 200
I (xxxx) WIFI_CLIENT: Upload successful!
```

**Errores**:
```
W (xxxx) WIFI_CLIENT: Failed to connect to SSID: MyNetwork
E (xxxx) WIFI_CLIENT: All saved networks failed, retrying in 10s
E (xxxx) WIFI_CLIENT: HTTP POST failed: 400 Bad Request
```

## Referencias

- **ESP-Hosted**: https://github.com/espressif/esp-hosted
- **ESP-IDF WiFi API**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html
- **SNTP**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_sntp.html

---

**Última actualización**: 2025-11-05
