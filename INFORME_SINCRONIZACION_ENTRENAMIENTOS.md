# INFORME COMPLETO: Sistema de Sincronización de Entrenamientos por Internet

## Índice
1. [Introducción](#introducción)
2. [Arquitectura del Sistema](#arquitectura-del-sistema)
3. [Estructura de Datos](#estructura-de-datos)
4. [Sistema WiFi](#sistema-wifi)
5. [Descarga de Planes de Entrenamiento](#descarga-de-planes-de-entrenamiento)
6. [Subida de Resultados de Entrenamiento](#subida-de-resultados-de-entrenamiento)
7. [Integración con la Interfaz de Usuario](#integración-con-la-interfaz-de-usuario)
8. [Servidores y APIs](#servidores-y-apis)
9. [Código Fuente Completo](#código-fuente-completo)
10. [Guía de Implementación](#guía-de-implementación)

---

## Introducción

Este sistema permite a una cinta de correr ESP32-P4 conectarse a internet vía WiFi para:
- **Descargar** planes de entrenamiento personalizados desde un servidor
- **Subir** resultados de entrenamientos completados a múltiples backends (Oracle Cloud, Google Drive)
- **Gestionar** credenciales WiFi con reconexión automática

---

## Arquitectura del Sistema

### Componentes Principales

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32-P4 Consola                      │
├─────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────┐   │
│  │            Interfaz de Usuario (LVGL)            │   │
│  │  - Selección de entrenamiento                    │   │
│  │  - Métricas en tiempo real                       │   │
│  │  - Botón de subida manual                        │   │
│  └────────────┬──────────────────────┬───────────────┘   │
│               │                      │                   │
│  ┌────────────▼──────────┐  ┌───────▼────────────────┐  │
│  │  TreadmillState       │  │   WiFi Client          │  │
│  │  - Métricas           │  │   - HTTP/HTTPS         │  │
│  │  - Estado entrenamiento│ │   - Descarga/Subida   │  │
│  │  - Flags de subida    │  │   - Sincronización    │  │
│  └───────────────────────┘  └────────┬───────────────┘  │
│                                      │                   │
│                         ┌────────────▼───────────────┐   │
│                         │    WiFi Manager            │   │
│                         │    - Credenciales NVS      │   │
│                         │    - Escaneo redes         │   │
│                         │    - Conexión automática   │   │
│                         └────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
                                 │
                    ┌────────────┴────────────┐
                    │                         │
            ┌───────▼────────┐       ┌───────▼──────────┐
            │  Oracle Cloud  │       │  Google Drive    │
            │  80.225.188.163│       │  (Apps Script)   │
            │  :8080         │       │                  │
            └────────────────┘       └──────────────────┘
```

### Archivos del Sistema

| Archivo | Líneas | Función |
|---------|--------|---------|
| `wifi_client.c` | 1177 | Cliente HTTP/HTTPS, descarga/subida |
| `wifi_client.h` | 126 | API pública del cliente WiFi |
| `wifi_manager.c` | 384 | Gestión de credenciales WiFi en NVS |
| `wifi_manager.h` | 111 | API del gestor WiFi |
| `treadmill_state.h` | 75 | Estructura de datos de entrenamiento |
| `treadmill_state.c` | 34 | Estado global |
| `ui.c` | ~2600 | Interfaz gráfica y lógica de entrenamiento |
| `ui_wifi.c` | 376 | UI de configuración WiFi |

---

## Estructura de Datos

### TreadmillState (treadmill_state.h)

```c
typedef struct {
    // Métricas del entrenamiento
    float speed_kmh;                    // Velocidad actual en km/h
    float climb_percent;                // Inclinación actual en %
    uint32_t elapsed_seconds;           // Tiempo transcurrido en segundos
    double total_distance_km;           // Distancia total recorrida en km

    // Estados de control
    bool is_stopped;                    // true si la cinta está parada
    bool is_cooling_down;               // true si en fase de enfriamiento
    bool is_resuming;                   // true si reanudando después de pausa

    // Monitor de frecuencia cardíaca BLE
    volatile uint16_t real_pulse;       // Pulsaciones reales del monitor
    volatile bool ble_connected;        // true si monitor BLE conectado

    // Datos calculados
    volatile int sim_pulse;             // Pulsaciones simuladas (fallback)
    volatile float sim_kcal;            // Calorías quemadas calculadas

    // Tipo de entrenamiento
    int selected_training;              // 1=Libre, 2=Itsaso, 3=Ina, 4=Alain, 5=Urko

    // Control de subida
    bool has_run_minimum_time;          // true si corrió al menos 10 segundos
    bool has_uploaded;                  // true si ya se subió exitosamente
    bool has_shown_welcome_message;     // true si se mostró mensaje inicial

    // Datos del usuario
    float user_weight_kg;               // Peso del usuario en kg
    bool weight_entered;                // true si el usuario ingresó su peso

    // Mantenimiento
    uint32_t total_running_seconds;     // Contador total de horas de uso (para cera)
} TreadmillState;

extern TreadmillState g_treadmill_state;
extern SemaphoreHandle_t g_state_mutex;
```

### Fórmula de Calorías (ACSM)

```c
// En ui.c líneas 424-434
float speed_m_min = speed_kmh * 1000.0f / 60.0f;  // km/h → m/min
float slope_decimal = climb_percent / 100.0f;      // % → decimal
float time_min = (interval_ms / 1000.0f) / 60.0f; // ms → min

float kcal = ((0.2 * speed_m_min + 0.9 * speed_m_min * slope_decimal + 3.5)
              * user_weight_kg * time_min) / 200.0;
```

---

## Sistema WiFi

### Gestión de Credenciales (wifi_manager.c)

#### Almacenamiento en NVS (Non-Volatile Storage)

```c
// Namespace NVS: "wifi_creds"
// Formato: clave = SSID, valor = password
#define NVS_NAMESPACE "wifi_creds"
#define SSID_ORDER_KEY "ssid_order"  // Lista ordenada de SSIDs por último uso

// Inicialización
esp_err_t wifi_manager_init(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_close(nvs_handle);
    }
    return err;
}
```

#### Guardar Credenciales

```c
// wifi_manager.c líneas 95-127
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    // Guardar password usando SSID como clave
    err = nvs_set_str(nvs_handle, ssid, password);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}
```

#### Cargar Credenciales

```c
// wifi_manager.c líneas 129-156
esp_err_t wifi_manager_load_credentials(const char *ssid, char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return err;

    size_t required_size = WIFI_MANAGER_MAX_PASSWORD_LEN;
    err = nvs_get_str(nvs_handle, ssid, password, &required_size);
    nvs_close(nvs_handle);

    return err;
}
```

#### Orden de Prioridad de Redes

```c
// wifi_manager.c líneas 273-335
// Al conectar exitosamente, se actualiza el orden de prioridad
esp_err_t wifi_manager_set_last_connected(const char *ssid) {
    // 1. Leer orden actual de NVS
    char* order_str = calloc(WIFI_MANAGER_MAX_NETWORKS, WIFI_MANAGER_MAX_SSID_LEN + 1);
    size_t required_size = WIFI_MANAGER_MAX_NETWORKS * (WIFI_MANAGER_MAX_SSID_LEN + 1);
    nvs_get_str(nvs_handle, SSID_ORDER_KEY, order_str, &required_size);

    // 2. Crear nuevo orden con ssid al principio
    char* new_order_str = calloc(WIFI_MANAGER_MAX_NETWORKS, WIFI_MANAGER_MAX_SSID_LEN + 1);
    snprintf(new_order_str, WIFI_MANAGER_MAX_SSID_LEN + 1, "%s", ssid);

    // 3. Agregar otros SSIDs (excepto el actual)
    char *token = strtok(order_str, ",");
    while (token != NULL) {
        if (strcmp(token, ssid) != 0) {
            strncat(new_order_str, ",", required_size - strlen(new_order_str) - 1);
            strncat(new_order_str, token, required_size - strlen(new_order_str) - 1);
        }
        token = strtok(NULL, ",");
    }

    // 4. Guardar nuevo orden
    nvs_set_str(nvs_handle, SSID_ORDER_KEY, new_order_str);
    nvs_commit(nvs_handle);

    return ESP_OK;
}
```

### Conexión Automática (wifi_client.c)

#### Inicialización WiFi

```c
// wifi_client.c líneas 243-279
esp_err_t wifi_client_init(void) {
    // 1. Crear mutex para descarga
    g_download_mutex = xSemaphoreCreateMutex();

    // 2. Crear event group para señales
    s_wifi_event_group = xEventGroupCreate();

    // 3. Inicializar stack TCP/IP
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // 4. Inicializar WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 5. Registrar event handlers
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, &instance_got_ip);

    // 6. Iniciar WiFi en modo estación
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    return ESP_OK;
}
```

#### Event Handler WiFi

```c
// wifi_client.c líneas 199-241
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {

    if (event_id == WIFI_EVENT_STA_START) {
        // WiFi iniciado - obtener lista de redes guardadas ordenadas
        wifi_manager_get_saved_ssids_ordered(s_saved_networks,
                                            WIFI_MANAGER_MAX_NETWORKS,
                                            &s_num_saved_networks);
        s_connection_attempt_index = 0;
        try_next_saved_network();

    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Desconexión - intentar siguiente red
        g_wifi_connected = false;
        g_internet_connected = false;
        try_next_saved_network();

    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        // IP obtenida - conexión exitosa
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        g_wifi_connected = true;

        // Actualizar prioridad de red
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            wifi_manager_set_last_connected((const char*)ap_info.ssid);
        }

        // Configurar DNS manualmente (Google DNS)
        esp_netif_dns_info_t dns_info;
        IP_ADDR4(&dns_info.ip, 8, 8, 8, 8);
        esp_netif_set_dns_info(event->esp_netif, ESP_NETIF_DNS_MAIN, &dns_info);

        // Verificar conectividad a internet
        xTaskCreate(&internet_check_task, "internet_check_task", 4096, NULL, 5, NULL);
    }
}
```

#### Intentar Siguiente Red

```c
// wifi_client.c líneas 166-197
static void try_next_saved_network(void) {
    if (s_connection_attempt_index < s_num_saved_networks) {
        // Hay redes guardadas disponibles
        wifi_config_t wifi_config = {0};
        strlcpy((char *)wifi_config.sta.ssid,
                s_saved_networks[s_connection_attempt_index].ssid,
                sizeof(wifi_config.sta.ssid));

        // Cargar password del NVS
        char password[WIFI_MANAGER_MAX_PASSWORD_LEN];
        esp_err_t err = wifi_manager_load_credentials((const char*)wifi_config.sta.ssid,
                                                      password);

        s_connection_attempt_index++;

        if (err == ESP_OK) {
            strlcpy((char *)wifi_config.sta.password, password,
                    sizeof(wifi_config.sta.password));
            esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            esp_wifi_connect();
        } else {
            // Password no encontrado, intentar siguiente
            try_next_saved_network();
        }
    } else {
        // No hay más redes - abrir selector WiFi
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        ui_open_wifi_list();
    }
}
```

#### Verificación de Conectividad a Internet

```c
// wifi_client.c líneas 107-135
void check_internet_connectivity(void) {
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
            g_internet_connected = true;
        } else {
            g_internet_connected = false;
        }
    } else {
        g_internet_connected = false;
    }

    esp_http_client_cleanup(client);
}
```

---

## Descarga de Planes de Entrenamiento

### Flujo de Descarga

```
Usuario selecciona      Descarga HTTP         Parseo de datos       Muestra en UI
entrenamiento   →   del servidor Oracle  →   (primera línea)   →   (pantalla principal)
(Itsaso/Ina)            (GET request)          del archivo            con instrucciones
```

### API de Descarga

```c
// wifi_client.c líneas 947-953
void wifi_download_plan(const char* username) {
    char url_buffer[256];
    const char* SERVER_IP = "80.225.188.163";
    const int SERVER_PORT = 8080;

    snprintf(url_buffer, sizeof(url_buffer),
             "http://%s:%d/get-plan/%s",
             SERVER_IP, SERVER_PORT, username);

    wifi_download_file(url_buffer);
}
```

### Tarea de Descarga HTTP

```c
// wifi_client.c líneas 434-541
static void http_download_task(void *pvParameters) {
    char *url = (char*)pvParameters;
    const int max_retries = 3;
    const int retry_delay_ms = 2000;
    bool download_success = false;

    // Esperar conexión WiFi (máximo 60 segundos)
    const int max_wait_time_ms = 60000;
    const int check_interval_ms = 500;
    int elapsed_time_ms = 0;

    while (elapsed_time_ms < max_wait_time_ms) {
        if (is_wifi_connected()) {
            // WiFi conectado - intentar descarga con reintentos
            for (int retry = 0; retry < max_retries && !download_success; retry++) {
                if (retry > 0) {
                    vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
                }

                // Configurar cliente HTTP
                esp_http_client_config_t config = {
                    .url = url,
                    .event_handler = _http_event_handler,
                    .user_agent = "ESP32",
                    .timeout_ms = 30000,
                    .skip_cert_common_name_check = true,
                };

                esp_http_client_handle_t client = esp_http_client_init(&config);
                if (!client) continue;

                esp_err_t err = esp_http_client_perform(client);

                // Verificar éxito
                if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    if (err == ESP_OK && g_downloaded_file_content != NULL &&
                        g_downloaded_file_size > 0) {
                        download_success = true;
                    }
                    xSemaphoreGive(g_download_mutex);
                }

                esp_http_client_cleanup(client);
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        elapsed_time_ms += check_interval_ms;
    }

    free(url);
    vTaskDelay(pdMS_TO_TICKS(500));  // Estabilización
    ui_loading_complete();  // Notificar a UI
    vTaskDelete(NULL);
}
```

### Event Handler HTTP (Descarga)

```c
// wifi_client.c líneas 282-380
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            // Limpiar buffer anterior
            if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (g_downloaded_file_content) {
                    heap_caps_free(g_downloaded_file_content);
                    g_downloaded_file_content = NULL;
                }
                g_downloaded_file_size = 0;
                received_len = 0;
                xSemaphoreGive(g_download_mutex);
            }
            break;

        case HTTP_EVENT_ON_HEADER:
            // Obtener tamaño del contenido
            if (strcasecmp(evt->header_key, "Content-Length") == 0) {
                int content_length = atoi(evt->header_value);
                if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    g_downloaded_file_size = content_length;
                    if (content_length > 0) {
                        g_downloaded_file_content = (char *) heap_caps_malloc(
                            content_length + 1, MALLOC_CAP_INTERNAL);
                    }
                    xSemaphoreGive(g_download_mutex);
                }
            }
            break;

        case HTTP_EVENT_ON_DATA:
            // Recibir datos
            if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                // Manejar chunked encoding (sin Content-Length)
                if (!g_downloaded_file_content && evt->data_len > 0) {
                    g_downloaded_file_size = 4096;  // Buffer inicial
                    g_downloaded_file_content = (char *) heap_caps_malloc(
                        g_downloaded_file_size + 1, MALLOC_CAP_INTERNAL);
                }

                // Expandir buffer si es necesario
                if (received_len + evt->data_len > g_downloaded_file_size) {
                    size_t new_size = received_len + evt->data_len + 1024;
                    char *new_buffer = (char *) heap_caps_realloc(
                        g_downloaded_file_content, new_size, MALLOC_CAP_INTERNAL);
                    if (new_buffer) {
                        g_downloaded_file_content = new_buffer;
                        g_downloaded_file_size = new_size - 1;
                    }
                }

                // Copiar datos
                if (g_downloaded_file_content) {
                    memcpy(g_downloaded_file_content + received_len,
                           evt->data, evt->data_len);
                    received_len += evt->data_len;
                }

                xSemaphoreGive(g_download_mutex);
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            // Finalizar descarga
            if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (g_downloaded_file_content) {
                    g_downloaded_file_content[received_len] = '\0';
                    g_downloaded_file_size = received_len;
                }
                xSemaphoreGive(g_download_mutex);
            }
            break;
    }
    return ESP_OK;
}
```

### Procesamiento en UI

```c
// ui.c líneas 2337-2380
void ui_loading_complete(void) {
    if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_downloaded_file_content != NULL && g_downloaded_file_size > 0) {
            // Extraer primera línea
            char *first_line = g_downloaded_file_content;
            char *newline = strchr(first_line, '\n');
            if (newline != NULL) {
                *newline = '\0';  // Null-terminate en el salto de línea
            }

            // Mostrar en área de información
            lv_label_set_text(ta_info, first_line);

            // Limpiar timer de mensaje temporal
            if (text_area_timer) {
                lv_timer_del(text_area_timer);
                text_area_timer = NULL;
            }

            // Liberar buffer
            free(g_downloaded_file_content);
            g_downloaded_file_content = NULL;
            g_downloaded_file_size = 0;
        } else {
            lv_label_set_text(ta_info, "Error en la descarga del entreno.");
        }
        xSemaphoreGive(g_download_mutex);
    }

    // Cargar pantalla principal y activar modo entrenamiento
    lv_scr_load(scr_main);
    cm_master_set_training_mode(true);
}
```

---

## Subida de Resultados de Entrenamiento

### Flujo de Subida

```
Usuario completa     Formatea datos        Sube a servidor       Notifica resultado
entrenamiento   →   (distancia/tiempo) →   (Oracle Cloud)    →   a la UI (éxito/error)
    (10+ seg)            con timestamp        HTTP POST
```

### Formato de Datos

```c
// Formato: "Distancia recorrida: XXXXm, Tiempo empleado: H:MM:SS"
// Ejemplo: "Distancia recorrida: 5000m, Tiempo empleado: 0:45:30"

// ui.c líneas 777-795
int distance_m = (int)(g_treadmill_state.total_distance_km * 1000);
uint32_t hours = g_treadmill_state.elapsed_seconds / 3600;
uint32_t minutes = (g_treadmill_state.elapsed_seconds % 3600) / 60;
uint32_t seconds = g_treadmill_state.elapsed_seconds % 60;

char upload_data[256];
snprintf(upload_data, sizeof(upload_data),
         "Distancia recorrida: %dm, Tiempo empleado: %u:%02u:%02u",
         distance_m, hours, minutes, seconds);
```

### API de Subida a Oracle Cloud

```c
// wifi_client.h líneas 77-84
void upload_to_oracle_ina(const char *text);
void upload_to_oracle_itsaso(const char *text);
```

### Funciones de Subida

```c
// wifi_client.c líneas 1153-1177
void upload_to_oracle_ina(const char *text) {
    const char* username = "ina";
    size_t buffer_size = strlen(username) + strlen(text) + 2;
    char* task_data = malloc(buffer_size);

    if (task_data) {
        snprintf(task_data, buffer_size, "%s|%s", username, text);
        xTaskCreate(&oracle_upload_task, "oracle_upload_ina",
                    8192, task_data, 5, NULL);
    } else {
        ui_upload_complete(false);
    }
}

void upload_to_oracle_itsaso(const char *text) {
    const char* username = "itsaso";
    size_t buffer_size = strlen(username) + strlen(text) + 2;
    char* task_data = malloc(buffer_size);

    if (task_data) {
        snprintf(task_data, buffer_size, "%s|%s", username, text);
        xTaskCreate(&oracle_upload_task, "oracle_upload_itsaso",
                    8192, task_data, 5, NULL);
    } else {
        ui_upload_complete(false);
    }
}
```

### Tarea de Subida a Oracle Cloud

```c
// wifi_client.c líneas 1124-1151
static void oracle_upload_task(void *pvParameters) {
    char *task_data = (char *)pvParameters;

    // Parsear formato "username|data"
    char *separator = strchr(task_data, '|');
    if (!separator) {
        free(task_data);
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    *separator = '\0';
    char *username = task_data;
    char *data_content = separator + 1;

    bool success = subirDatosOracle(username, data_content);

    ui_upload_complete(success);
    free(task_data);
    vTaskDelete(NULL);
}
```

### Función de Subida HTTP POST a Oracle

```c
// wifi_client.c líneas 1062-1122
static bool subirDatosOracle(const char* username, const char* datos_a_enviar) {
    char url_buffer[256];
    const char* SERVER_IP = "80.225.188.163";
    const int SERVER_PORT = 8080;

    // 1. Construir URL
    snprintf(url_buffer, sizeof(url_buffer),
             "http://%s:%d/upload/%s",
             SERVER_IP, SERVER_PORT, username);

    // 2. Configurar HTTP POST
    esp_http_client_config_t config = {
        .url = url_buffer,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }

    // 3. Añadir headers
    esp_http_client_set_header(client, "Content-Type", "text/plain");

    // 4. Adjuntar datos
    esp_http_client_set_post_field(client, datos_a_enviar, strlen(datos_a_enviar));

    // 5. Ejecutar y leer respuesta
    bool success = false;
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);

        char response_buffer[512] = {0};
        int total_read_len = 0;
        int read_len;

        while ((read_len = esp_http_client_read(client,
                                                 response_buffer + total_read_len,
                                                 sizeof(response_buffer) - total_read_len - 1)) > 0) {
            total_read_len += read_len;
        }

        if (status_code == 200) {
            success = true;
        }
    }

    // 6. Cleanup
    esp_http_client_cleanup(client);
    return success;
}
```

### Subida Alternativa: Google Drive (Apps Script)

```c
// wifi_client.c líneas 771-894
static void google_script_upload_task(void *pvParameters) {
    char *url_and_data = (char *)pvParameters;

    // Parsear "url|data"
    char *separator = strchr(url_and_data, '|');
    *separator = '\0';
    char *google_script_url = url_and_data;
    char *data_content = separator + 1;

    // URL encode los datos
    char *encoded_data = malloc(strlen(data_content) * 3 + 1);
    url_encode(data_content, encoded_data, strlen(data_content) * 3 + 1);

    // Construir URL completa
    char *full_url = malloc(strlen(google_script_url) + strlen(encoded_data) + 20);
    snprintf(full_url, strlen(google_script_url) + strlen(encoded_data) + 20,
             "%s?data=%s", google_script_url, encoded_data);

    // Configurar HTTP GET (Google Apps Script usa GET)
    esp_http_client_config_t config = {
        .url = full_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .skip_cert_common_name_check = true,  // Workaround para ESP32-P4
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent",
                               "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

    esp_err_t err = esp_http_client_perform(client);

    bool success = false;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 || status_code == 302) {
            success = true;
        }
    }

    esp_http_client_cleanup(client);
    free(full_url);
    free(encoded_data);
    free(url_and_data);

    ui_upload_complete(success);
    vTaskDelete(NULL);
}
```

### URL Encoding Helper

```c
// wifi_client.c líneas 738-766
static void url_encode(const char *src, char *dst, size_t dst_size) {
    const char *hex = "0123456789ABCDEF";
    size_t dst_idx = 0;

    while (*src && dst_idx < dst_size - 1) {
        if ((*src >= 'A' && *src <= 'Z') ||
            (*src >= 'a' && *src <= 'z') ||
            (*src >= '0' && *src <= '9') ||
            *src == '-' || *src == '_' || *src == '.' || *src == '~') {
            // Caracter seguro, copiar tal cual
            dst[dst_idx++] = *src;
        } else if (*src == ' ') {
            // Espacio se convierte en +
            dst[dst_idx++] = '+';
        } else {
            // Codificar como %XX
            if (dst_idx + 3 < dst_size) {
                dst[dst_idx++] = '%';
                dst[dst_idx++] = hex[(*src >> 4) & 0x0F];
                dst[dst_idx++] = hex[*src & 0x0F];
            } else {
                break;
            }
        }
        src++;
    }
    dst[dst_idx] = '\0';
}
```

### Subida Alternativa: InfinityFree (Legacy)

```c
// wifi_client.c líneas 543-736
// NOTA: Este método incluye sincronización SNTP para timestamp
static void upload_task(void *pvParameters) {
    char *data_and_file = (char *)pvParameters;

    // Parsear "filename|data"
    char *separator = strchr(data_and_file, '|');
    *separator = '\0';
    char *filename = data_and_file;
    char *data_content = separator + 1;

    // Sincronizar hora con SNTP
    if (!sync_time_sntp()) {
        ui_upload_complete(false);
        vTaskDelete(NULL);
        return;
    }

    // Obtener timestamp actual
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    char buffer_fecha[64];
    strftime(buffer_fecha, sizeof(buffer_fecha), "%Y-%m-%d %H:%M:%S", &timeinfo);

    // Crear mensaje con timestamp
    char *buffer_mensaje = malloc(strlen(buffer_fecha) + strlen(data_content) + 10);
    snprintf(buffer_mensaje, strlen(buffer_fecha) + strlen(data_content) + 10,
             "%s - %s\n", buffer_fecha, data_content);

    // Construir URL con parámetros
    char url_with_params[256];
    snprintf(url_with_params, sizeof(url_with_params),
             "%s?key=%s&filename=%s", UPLOAD_URL, API_KEY, filename);

    // Configurar HTTP POST con headers de navegador (anti-bot)
    esp_http_client_config_t config = {
        .url = url_with_params,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_header(client, "Accept", "*/*");

    // Enviar datos
    esp_http_client_set_post_field(client, buffer_mensaje, strlen(buffer_mensaje));
    esp_err_t err = esp_http_client_open(client, strlen(buffer_mensaje));

    if (err == ESP_OK) {
        esp_http_client_write(client, buffer_mensaje, strlen(buffer_mensaje));
        esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);

        // Leer respuesta completa para verificar anti-bot
        // (verificar que no sea HTML sino texto plano)
    }

    esp_http_client_cleanup(client);
    free(buffer_mensaje);
    free(data_and_file);

    ui_upload_complete(success);
    vTaskDelete(NULL);
}
```

### Sincronización SNTP

```c
// wifi_client.c líneas 383-431
static bool sync_time_sntp(void) {
    // Inicializar SNTP si no está inicializado
    if (!g_sntp_initialized) {
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
        return true;  // Ya sincronizado
    }

    // Esperar sincronización (máximo 30 segundos)
    int retry = 0;
    const int retry_count = 30;

    while (timeinfo.tm_year + 1900 < 2024 && retry < retry_count) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }

    if (timeinfo.tm_year + 1900 < 2024) {
        return false;  // Timeout
    }

    return true;
}
```

---

## Integración con la Interfaz de Usuario

### Selección de Entrenamiento

```c
// ui.c líneas 1216-1254

// Entrenamiento Itsaso - con descarga de plan
static void training_itsaso_event_cb(lv_event_t *e) {
    audio_play_beep();

    // Limpiar timer de WiFi
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 2;  // 2 = Itsaso
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);

    lv_scr_load(scr_loading);  // Pantalla de carga
    wifi_download_plan("itsaso");  // Descargar plan
}

// Entrenamiento Ina - con descarga de plan
static void training_ina_event_cb(lv_event_t *e) {
    audio_play_beep();

    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 3;  // 3 = Ina
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);

    lv_scr_load(scr_loading);
    wifi_download_plan("ina");
}

// Entrenamiento Libre - sin descarga
static void training_free_event_cb(lv_event_t *e) {
    audio_play_beep();

    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 1;  // 1 = Libre
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);

    lv_scr_load(scr_main);  // Directamente a pantalla principal
    cm_master_set_training_mode(true);
    set_info_text_persistent("Selecciona una velocidad para comenzar");
}
```

### Subida Manual de Entrenamiento

```c
// ui.c líneas 766-810
static void upload_training_event_cb(lv_event_t *e) {
    audio_play_beep();

    // Verificar WiFi conectado
    if (!is_wifi_connected()) {
        set_info_text("WiFi no conectado. No se puede subir.");
        return;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    int training = g_treadmill_state.selected_training;

    // Obtener datos del entrenamiento
    double distance_km = g_treadmill_state.total_distance_km;
    uint32_t total_seconds = g_treadmill_state.elapsed_seconds;
    xSemaphoreGive(g_state_mutex);

    // Calcular distancia en metros
    int distance_m = (int)(distance_km * 1000);

    // Calcular tiempo en formato H:MM:SS
    uint32_t hours = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;

    // Crear mensaje a enviar
    char upload_data[256];
    snprintf(upload_data, sizeof(upload_data),
             "Distancia recorrida: %dm, Tiempo empleado: %u:%02u:%02u",
             distance_m, hours, minutes, seconds);

    // Cambiar a pantalla de subida
    lv_scr_load(scr_uploading);

    // Subir según tipo de entrenamiento
    if (training == 2) {
        // Itsaso
        upload_to_oracle_itsaso(upload_data);
    } else if (training == 3) {
        // Ina
        upload_to_oracle_ina(upload_data);
    }
}
```

### Mostrar Botón de Subida

```c
// ui.c líneas 437-450
// El botón de subida se muestra cuando:
// 1. La velocidad es 0 (cinta parada)
// 2. El tiempo mínimo ha sido alcanzado (10+ segundos)
// 3. No se ha subido aún
// 4. El entrenamiento es Itsaso o Ina (training 2 o 3)

if (g_treadmill_state.speed_kmh == 0) {
    if (g_treadmill_state.has_run_minimum_time &&
        !g_treadmill_state.has_uploaded &&
        (g_treadmill_state.selected_training == 2 ||
         g_treadmill_state.selected_training == 3)) {
        // Mostrar botón
        lv_obj_clear_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
    }
} else {
    // Ocultar botón si la cinta se mueve
    lv_obj_add_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
}
```

### Callback de Subida Completa

```c
// ui.c líneas 2382-2403
void ui_upload_complete(bool success) {
    if (success) {
        // Marcar como subido
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.has_uploaded = true;
        xSemaphoreGive(g_state_mutex);

        // Mostrar mensaje de éxito
        lv_label_set_text(lbl_upload_status, "Subida exitosa!");

        // Volver a pantalla principal después de 2 segundos
        lv_timer_t* return_timer = lv_timer_create(
            [](lv_timer_t* timer) {
                lv_scr_load(scr_main);
                lv_timer_del(timer);
            }, 2000, NULL);
    } else {
        // Mostrar mensaje de error
        lv_label_set_text(lbl_upload_status, "Error en la subida");

        // Volver después de 2 segundos
        lv_timer_t* return_timer = lv_timer_create(
            [](lv_timer_t* timer) {
                lv_scr_load(scr_main);
                lv_timer_del(timer);
            }, 2000, NULL);
    }
}
```

---

## Servidores y APIs

### Oracle Cloud Server

**Configuración:**
```c
IP: 80.225.188.163
Puerto: 8080
Protocolo: HTTP
```

**Endpoints:**

1. **Descarga de Plan:**
   ```
   GET http://80.225.188.163:8080/get-plan/{username}

   Parámetros:
   - username: "ina" o "itsaso"

   Respuesta:
   Content-Type: text/plain
   Body: Texto plano con instrucciones del plan de entrenamiento

   Ejemplo:
   "Calentamiento: 5 min a 6 km/h\nTrabajo: 20 min a 10 km/h\nEnfriamiento: 5 min a 5 km/h"
   ```

2. **Subida de Entrenamiento:**
   ```
   POST http://80.225.188.163:8080/upload/{username}

   Parámetros:
   - username: "ina" o "itsaso"

   Headers:
   Content-Type: text/plain

   Body:
   "Distancia recorrida: XXXXm, Tiempo empleado: H:MM:SS"

   Ejemplo:
   "Distancia recorrida: 5000m, Tiempo empleado: 0:45:30"

   Respuesta:
   HTTP 200 OK
   ```

### Google Apps Script (Google Drive)

**URLs:**

```c
// Ina
https://script.google.com/macros/s/AKfycbxCjlHprXi40arHypxwlsWov-_zrejxzbOLiIhFZo7ffizBNK_z_oNG09kBk1qS5VJ-kw/exec

// Itsaso
https://script.google.com/macros/s/AKfycbxDA9al2_Yewn3ReoThMDZYYTrJNNoNTbKG6FV4upAWCRmUwjK9NGK5Ae9lZRb3taB_pw/exec
```

**Método:**
```
GET {url}?data={url_encoded_data}

Ejemplo:
GET https://script.google.com/macros/s/.../exec?data=Distancia+recorrida%3A+5000m%2C+Tiempo+empleado%3A+0%3A45%3A30

Respuesta:
HTTP 200 OK o 302 Redirect (ambos indican éxito)
```

**Características:**
- Usa HTTPS (requiere `skip_cert_common_name_check = true` en ESP32-P4)
- Timeout de 30 segundos (los Google Scripts pueden ser lentos)
- Los datos se pasan como parámetro GET URL-encoded
- El script almacena los datos en una hoja de cálculo de Google Drive

### InfinityFree (Legacy - ya no se usa activamente)

**Configuración:**
```c
URL: http://entrenadorpersonalia.ct.ws/upload.php
API Key: "Spoofer86"
```

**Archivos:**
```c
entreno_cinta_ina.txt
entreno_cinta_itsaso.txt
```

**Método:**
```
POST http://entrenadorpersonalia.ct.ws/upload.php?key={API_KEY}&filename={filename}

Headers:
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36
Content-Type: text/plain
Accept: */*
Referer: http://entrenadorpersonalia.ct.ws/

Body:
"YYYY-MM-DD HH:MM:SS - Distancia recorrida: XXXXm, Tiempo empleado: H:MM:SS\n"

Ejemplo:
"2025-11-24 14:30:00 - Distancia recorrida: 5000m, Tiempo empleado: 0:45:30\n"
```

**Notas:**
- Requiere headers de navegador para bypasear protección anti-bot
- Requiere sincronización SNTP para timestamp
- La respuesta debe ser texto plano (no HTML) para verificar éxito

### Verificación de Conectividad

```
GET http://connectivitycheck.gstatic.com/generate_204

Respuesta esperada:
HTTP 204 No Content

Indica: Acceso a internet disponible
```

---

## Código Fuente Completo

### wifi_client.h

```c
#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Initializes the WiFi client, configures it as a station, and starts the connection process.
 */
esp_err_t wifi_client_init(void);

/**
 * @brief Manually trigger file download from server
 */
void wifi_download_file(const char *url);

/**
 * @brief Global pointer to downloaded file content (null-terminated string)
 */
extern char *g_downloaded_file_content;

/**
 * @brief Size of the downloaded file in bytes (excluding null terminator)
 */
extern int g_downloaded_file_size;

/**
 * @brief Mutex for protecting access to download buffer
 */
extern SemaphoreHandle_t g_download_mutex;

/**
 * @brief Triggers an asynchronous upload for Ina's training file.
 */
void upload_to_ina(int number);

/**
 * @brief Triggers an asynchronous upload for Itsaso's training file.
 */
void upload_to_itsaso(int number);

/**
 * @brief Uploads text data to Ina's training file.
 */
void upload_text_to_ina(const char *text);

/**
 * @brief Uploads text data to Itsaso's training file.
 */
void upload_text_to_itsaso(const char *text);

/**
 * @brief Uploads training data to Oracle Cloud for Ina.
 */
void upload_to_oracle_ina(const char *text);

/**
 * @brief Uploads training data to Oracle Cloud for Itsaso.
 */
void upload_to_oracle_itsaso(const char *text);

/**
 * @brief Downloads a training plan for a specific user.
 */
void wifi_download_plan(const char* username);

/**
 * @brief Checks if the WiFi is currently connected and has an IP address.
 */
bool is_wifi_connected(void);

bool is_internet_connected(void);

void check_internet_connectivity(void);

extern bool g_internet_connected;

/**
 * @brief Connect to a WiFi network with given credentials
 */
esp_err_t wifi_client_connect(const char *ssid, const char *password);

/**
 * @brief Sends data via HTTP POST to a specific PHP script with multiple headers to simulate a browser.
 */
void subirDatosPOST_Avanzado(const char* filename, const char* datos_a_enviar);

#endif // WIFI_CLIENT_H
```

### wifi_manager.h

```c
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>

#define WIFI_MANAGER_MAX_NETWORKS 20
#define WIFI_MANAGER_MAX_SSID_LEN 32
#define WIFI_MANAGER_MAX_PASSWORD_LEN 64

/**
 * @brief Structure to hold scanned WiFi network information
 */
typedef struct {
    char ssid[WIFI_MANAGER_MAX_SSID_LEN];
    int8_t rssi;
    wifi_auth_mode_t auth_mode;
} wifi_network_info_t;

/**
 * @brief Initialize the WiFi manager module
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Scan for available WiFi networks
 */
esp_err_t wifi_manager_scan_networks(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found);

/**
 * @brief Save WiFi credentials to NVS
 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Load WiFi credentials from NVS
 */
esp_err_t wifi_manager_load_credentials(const char *ssid, char *password);

/**
 * @brief Check if credentials exist for a given SSID
 */
bool wifi_manager_has_credentials(const char *ssid);

/**
 * @brief Delete stored credentials for a given SSID
 */
esp_err_t wifi_manager_delete_credentials(const char *ssid);

/**
 * @brief Get the currently connected SSID
 */
esp_err_t wifi_manager_get_current_ssid(char *ssid);

/**
 * @brief Set the last successfully connected SSID to prioritize it in the future
 */
esp_err_t wifi_manager_set_last_connected(const char *ssid);

/**
 * @brief Get all saved SSIDs from NVS, ordered by most recently used
 */
esp_err_t wifi_manager_get_saved_ssids_ordered(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found);

/**
 * @brief Get all saved SSIDs from NVS
 */
esp_err_t wifi_manager_get_saved_ssids(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found);

#endif // WIFI_MANAGER_H
```

### treadmill_state.h

```c
#ifndef TREADMILL_STATE_H
#define TREADMILL_STATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#define MAX_SPEED_KMH 19.5f
#define MAX_CLIMB_PERCENT 15.0f
#define DEFAULT_USER_WEIGHT_KG 70.0f

typedef enum {
    SET_MODE_NONE,
    SET_MODE_SPEED,
    SET_MODE_CLIMB,
    SET_MODE_WEIGHT
} set_mode_t;

typedef enum {
    RAMP_MODE_NORMAL,
    RAMP_MODE_STOP_STOP,
    RAMP_MODE_COOLDOWN_STOP,
    RAMP_MODE_STOP_RESUME,
    RAMP_MODE_COOLDOWN_RESUME,
} ramp_mode_t;

typedef struct {
    float speed_kmh;
    float climb_percent;
    float target_climb_percent;
    uint32_t elapsed_seconds;
    double total_distance_km;
    bool is_stopped;
    bool is_cooling_down;
    bool is_resuming;
    bool resume_from_stop;
    float speed_before_stop;
    float target_speed;
    float cooldown_climb_ramp_rate;

    // Data from BLE Heart Rate monitor
    volatile uint16_t real_pulse;
    volatile bool ble_connected;

    // Simulated data (fallback)
    volatile int sim_pulse;
    volatile float sim_kcal;
    set_mode_t set_mode;
    char set_buffer[4];
    int set_digit_index;
    lv_timer_t *blink_timer;
    bool blink_state;
    ramp_mode_t ramp_mode;

    // Training type (1=Free, 2=Itsaso, 3=Ina, 4=Alain, 5=Urko)
    int selected_training;

    // Training completion tracking
    bool has_run_minimum_time;  // true if treadmill ran for at least 10 seconds
    bool has_uploaded;           // true if training data has been uploaded successfully
    bool has_shown_welcome_message;

    // User weight
    float user_weight_kg;
    bool weight_entered;

    // Wax maintenance tracking
    uint32_t total_running_seconds;
} TreadmillState;

extern TreadmillState g_treadmill_state;
extern SemaphoreHandle_t g_state_mutex;

#endif // TREADMILL_STATE_H
```

---

## Guía de Implementación

### Para Replicar en Otro Dispositivo ESP32

#### Paso 1: Configuración Inicial

```c
// En main.c
void app_main(void) {
    // 1. Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Inicializar WiFi Manager
    wifi_manager_init();

    // 3. Inicializar WiFi Client
    wifi_client_init();

    // 4. Crear mutex para estado
    g_state_mutex = xSemaphoreCreateMutex();
}
```

#### Paso 2: Estructura de Datos

```c
// Definir estructura de estado global
TreadmillState g_treadmill_state = {
    .speed_kmh = 0.0f,
    .climb_percent = 0.0f,
    .elapsed_seconds = 0,
    .total_distance_km = 0.0,
    .selected_training = 0,
    .has_run_minimum_time = false,
    .has_uploaded = false,
    .user_weight_kg = DEFAULT_USER_WEIGHT_KG,
    .weight_entered = false,
};
```

#### Paso 3: Implementar Descarga

```c
// En tu código de UI o lógica de negocio

// Para descargar un plan:
void select_training(const char* username) {
    // Cambiar a pantalla de carga
    show_loading_screen();

    // Descargar plan
    wifi_download_plan(username);  // "ina" o "itsaso"
}

// Callback cuando descarga completa:
void ui_loading_complete(void) {
    if (xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_downloaded_file_content != NULL && g_downloaded_file_size > 0) {
            // Procesar contenido descargado
            char *first_line = g_downloaded_file_content;
            char *newline = strchr(first_line, '\n');
            if (newline != NULL) {
                *newline = '\0';
            }

            // Mostrar en UI
            display_training_instructions(first_line);

            // Liberar memoria
            free(g_downloaded_file_content);
            g_downloaded_file_content = NULL;
            g_downloaded_file_size = 0;
        } else {
            display_error("Error en la descarga");
        }
        xSemaphoreGive(g_download_mutex);
    }

    show_main_screen();
}
```

#### Paso 4: Implementar Subida

```c
// Cuando el entrenamiento termina:
void upload_training_results(void) {
    if (!is_wifi_connected()) {
        display_error("WiFi no conectado");
        return;
    }

    // Obtener datos del entrenamiento
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    int training_type = g_treadmill_state.selected_training;
    double distance_km = g_treadmill_state.total_distance_km;
    uint32_t total_seconds = g_treadmill_state.elapsed_seconds;
    xSemaphoreGive(g_state_mutex);

    // Formatear datos
    int distance_m = (int)(distance_km * 1000);
    uint32_t hours = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;

    char upload_data[256];
    snprintf(upload_data, sizeof(upload_data),
             "Distancia recorrida: %dm, Tiempo empleado: %u:%02u:%02u",
             distance_m, hours, minutes, seconds);

    // Subir según tipo de entrenamiento
    show_uploading_screen();

    if (training_type == 2) {
        upload_to_oracle_itsaso(upload_data);
    } else if (training_type == 3) {
        upload_to_oracle_ina(upload_data);
    }
}

// Callback cuando subida completa:
void ui_upload_complete(bool success) {
    if (success) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.has_uploaded = true;
        xSemaphoreGive(g_state_mutex);

        display_success("Subida exitosa!");
    } else {
        display_error("Error en la subida");
    }

    // Volver a pantalla principal después de 2 segundos
    delay_and_return_to_main(2000);
}
```

#### Paso 5: Configuración de CMakeLists.txt

```cmake
idf_component_register(
    SRCS
        "main.c"
        "wifi_client.c"
        "wifi_manager.c"
        "treadmill_state.c"
        "ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_http_client
        esp_netif
        lwip
        esp_event
)
```

#### Paso 6: Configuración de sdkconfig

```
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM=32
CONFIG_ESP32_WIFI_AMPDU_TX_ENABLED=y
CONFIG_ESP32_WIFI_AMPDU_RX_ENABLED=y

CONFIG_LWIP_MAX_SOCKETS=10
CONFIG_LWIP_SO_REUSE=y

CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_ESP_TLS_INSECURE=y
CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y
```

### Configuración del Servidor (Oracle Cloud)

#### Servidor Node.js/Express Ejemplo

```javascript
const express = require('express');
const fs = require('fs').promises;
const app = express();

app.use(express.text());

// Descargar plan
app.get('/get-plan/:username', async (req, res) => {
    const username = req.params.username;
    try {
        const plan = await fs.readFile(`./plans/${username}.txt`, 'utf8');
        res.type('text/plain').send(plan);
    } catch (error) {
        res.status(404).send('Plan no encontrado');
    }
});

// Subir entrenamiento
app.post('/upload/:username', async (req, res) => {
    const username = req.params.username;
    const data = req.body;

    const timestamp = new Date().toISOString();
    const entry = `${timestamp} - ${data}\n`;

    try {
        await fs.appendFile(`./workouts/${username}.txt`, entry);
        res.status(200).send('OK');
    } catch (error) {
        res.status(500).send('Error al guardar');
    }
});

app.listen(8080, '0.0.0.0', () => {
    console.log('Servidor escuchando en puerto 8080');
});
```

#### Formato de Archivo de Plan (`plans/ina.txt`)

```
Calentamiento: 5 min a 6 km/h, 0% inclinación
Trabajo principal: 20 min a 10 km/h, 2% inclinación
Intervalos: 5x (2 min a 12 km/h, 1 min a 8 km/h)
Enfriamiento: 5 min a 5 km/h, 0% inclinación
```

### Resolución de Problemas Comunes

#### Problema 1: WiFi no conecta

```c
// Verificar:
1. Credenciales guardadas en NVS
2. Red WiFi disponible
3. Contraseña correcta
4. Canal WiFi compatible (ESP32 solo soporta 2.4GHz)

// Debug:
ESP_LOGI(TAG, "SSIDs guardados: %d", s_num_saved_networks);
for (int i = 0; i < s_num_saved_networks; i++) {
    ESP_LOGI(TAG, "Red %d: %s", i, s_saved_networks[i].ssid);
}
```

#### Problema 2: Descarga falla

```c
// Verificar:
1. WiFi conectado (is_wifi_connected())
2. Internet disponible (is_internet_connected())
3. Servidor accesible (ping al servidor)
4. URL correcta
5. Timeout suficiente (30 segundos)

// Debug:
ESP_LOGI(TAG, "WiFi: %d, Internet: %d",
         is_wifi_connected(), is_internet_connected());
ESP_LOGI(TAG, "Descargando de: %s", url);
ESP_LOGI(TAG, "Bytes recibidos: %d", received_len);
```

#### Problema 3: Subida falla

```c
// Verificar:
1. Formato de datos correcto
2. Servidor respondiendo
3. Certificados SSL (si HTTPS)
4. Headers correctos

// Debug:
ESP_LOGI(TAG, "Subiendo: %s", upload_data);
ESP_LOGI(TAG, "Status code: %d", status_code);
ESP_LOGI(TAG, "Respuesta: %s", response_buffer);
```

#### Problema 4: Memoria insuficiente

```c
// Soluciones:
1. Aumentar tamaño de stack de tareas
   xTaskCreate(&task, "name", 16384, ...);  // 16KB stack

2. Usar heap_caps_malloc para buffers grandes
   char *buffer = (char*) heap_caps_malloc(size, MALLOC_CAP_INTERNAL);

3. Liberar memoria inmediatamente después de usar
   free(buffer);
   buffer = NULL;

4. Verificar heap disponible
   ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
```

### Checklist de Implementación

- [ ] Inicializar NVS
- [ ] Inicializar WiFi Manager
- [ ] Inicializar WiFi Client
- [ ] Crear estructura TreadmillState
- [ ] Crear mutex para proteger estado
- [ ] Crear mutex para descargas
- [ ] Implementar UI de selección WiFi
- [ ] Implementar UI de selección de entrenamiento
- [ ] Implementar pantalla de carga
- [ ] Implementar pantalla de subida
- [ ] Implementar callback ui_loading_complete()
- [ ] Implementar callback ui_upload_complete()
- [ ] Configurar servidor Oracle Cloud
- [ ] Probar descarga de plan
- [ ] Probar subida de entrenamiento
- [ ] Verificar reconexión automática WiFi
- [ ] Verificar manejo de errores
- [ ] Verificar liberación de memoria

---

## Conclusiones

Este sistema implementa una solución completa de sincronización de entrenamientos para dispositivos ESP32, con las siguientes características:

**Ventajas:**
- Reconexión automática a redes guardadas
- Múltiples backends de respaldo (Oracle, Google Drive)
- Manejo robusto de errores con reintentos
- Sincronización de tiempo SNTP
- Protección de datos con mutex
- UI intuitiva con feedback visual

**Consideraciones:**
- Requiere WiFi 2.4GHz
- Timeout de 30-60 segundos para operaciones de red
- Memoria limitada en ESP32 (usar heap_caps cuidadosamente)
- Certificados SSL pueden requerir configuración especial en ESP32-P4

**Uso de memoria aproximado:**
- Stack de tarea HTTP: 8-16KB
- Buffer de descarga: 4KB inicial (expansible)
- Credenciales NVS: ~100 bytes por red
- Estado global: ~200 bytes

Este informe proporciona toda la información necesaria para implementar un sistema similar en cualquier dispositivo ESP32 con conectividad WiFi.
