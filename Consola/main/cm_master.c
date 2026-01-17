/**
 * @file cm_master.c
 * @brief Módulo maestro con protocolo ASCII simple
 *
 * Protocolo simplificado: Comandos ASCII terminados en \n
 * Envío directo UART sin byte stuffing ni CRC
 */

#include "cm_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "CM_MASTER";

// ============================================================================
// CONSTANTES
// ============================================================================

#define UART_BUF_SIZE            512
#define LINE_BUFFER_SIZE         128
#define SYNC_INTERVAL_MS         100  // SYNC cada 100ms
#define CONNECTION_TIMEOUT_MS    1000 // Sin respuesta en 1s = desconectado

// ============================================================================
// VARIABLES PRIVADAS
// ============================================================================

/** Mutex para proteger variables compartidas */
static SemaphoreHandle_t g_master_mutex = NULL;

/** Estado de conexión */
static bool g_connected = false;

/** Timestamp de la última respuesta recibida */
static int64_t g_last_response_us = 0;

/** Variables de estado del esclavo (última lectura) */
static float g_real_speed_kmh = 0.0f;
static float g_current_incline_pct = 0.0f;
static float g_vfd_freq_hz = 0.0f;
static uint8_t g_vfd_fault = 0;
static uint8_t g_head_fan_state = 0;
static uint8_t g_chest_fan_state = 0;
static uint8_t g_incline_sensor_fault = 0;  // Error crítico del sensor de fin de carrera

/** Variables de control (targets) */
static float g_target_speed_kmh = 0.0f;
static float g_target_incline_pct = 0.0f;
static uint8_t g_target_head_fan = 0;
static uint8_t g_target_chest_fan = 0;
static uint8_t g_target_wax_pump = 0;
static bool g_training_mode = false;  // false = pantalla inicial, true = entrenando

/** Handles de tareas */
static TaskHandle_t g_master_task_handle = NULL;
static TaskHandle_t g_uart_rx_task_handle = NULL;

// ============================================================================
// FUNCIONES PRIVADAS - ENVÍO
// ============================================================================

/**
 * @brief Envía una línea de texto por UART
 */
static esp_err_t send_line(const char *line) {
    int len = strlen(line);
    int written = uart_write_bytes(CM_MASTER_UART_PORT, line, len);
    if (written < 0) {
        ESP_LOGE(TAG, "Error al enviar línea por UART");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Enviado: %s", line);
    return ESP_OK;
}

/**
 * @brief Envía comando con formato "CMD=valor\n"
 */

/**
 * @brief Envía comando con valor entero
 */
static esp_err_t send_command_int(const char *cmd, int value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s=%d\n", cmd, value);
    return send_line(buffer);
}

/**
 * @brief Envía SYNC con todos los objetivos: SYNC=speed,incline,fan_head,fan_chest,wax,training_mode
 */
static esp_err_t send_sync(float speed, float incline, uint8_t fan_head, uint8_t fan_chest, uint8_t wax, bool training_mode) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "SYNC=%.2f,%.2f,%d,%d,%d,%d\n",
             speed, incline, fan_head, fan_chest, wax, training_mode ? 1 : 0);
    return send_line(buffer);
}

// ============================================================================
// FUNCIONES PRIVADAS - RECEPCIÓN Y PARSING
// ============================================================================

/**
 * @brief Procesa respuesta DATA=<speed>,<incline>,<vfd_freq>,<vfd_fault>,<fan_head>,<fan_chest>,<incline_fault>
 */
static void process_data_response(const char *line) {
    float speed, incline, vfd_freq;
    int vfd_fault, fan_head, fan_chest, incline_fault = 0;

    // Validación de formato: debe tener al menos 5 comas y formato numérico
    int comma_count = 0;
    const char *p = line;
    while (*p) {
        if (*p == ',') comma_count++;
        p++;
    }
    
    // Debe tener al menos 5 comas (6 campos mínimo)
    if (comma_count < 5) {
        ESP_LOGW(TAG, "Error al parsear DATA (formato inválido): %s", line);
        return;
    }

    // Intentar parsear formato nuevo (7 campos)
    int parsed = sscanf(line, "DATA=%f,%f,%f,%d,%d,%d,%d",
                        &speed, &incline, &vfd_freq,
                        &vfd_fault, &fan_head, &fan_chest, &incline_fault);

    // Compatibilidad con formato antiguo (6 campos)
    if (parsed < 6) {
        ESP_LOGW(TAG, "Error al parsear DATA (campos insuficientes): %s", line);
        return;
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_real_speed_kmh = speed;
    g_current_incline_pct = incline;
    g_vfd_freq_hz = vfd_freq;
    g_vfd_fault = (uint8_t)vfd_fault;
    g_head_fan_state = (uint8_t)fan_head;
    g_chest_fan_state = (uint8_t)fan_chest;
    g_incline_sensor_fault = (uint8_t)incline_fault;
    g_last_response_us = esp_timer_get_time();
    g_connected = true;
    xSemaphoreGive(g_master_mutex);

    ESP_LOGD(TAG, "DATA: speed=%.2f incline=%.1f vfd_freq=%.2f vfd_fault=%d fans=%d,%d incline_fault=%d",
             speed, incline, vfd_freq, vfd_fault, fan_head, fan_chest, incline_fault);

    // Alerta crítica si se detecta fallo del sensor
    if (incline_fault != 0) {
        ESP_LOGE(TAG, "═══════════════════════════════════════════════════");
        ESP_LOGE(TAG, "  ALERTA CRÍTICA: Fallo del sensor de inclinación");
        ESP_LOGE(TAG, "  Sistema bloqueado - Requiere servicio técnico");
        ESP_LOGE(TAG, "═══════════════════════════════════════════════════");
    }
}

/**
 * @brief Verifica si una línea contiene solo caracteres ASCII imprimibles
 */
static bool is_line_valid(const char *line) {
    if (!line || strlen(line) < 3) return false;
    for (int i = 0; line[i] != '\0'; i++) {
        if ((unsigned char)line[i] < 32 || (unsigned char)line[i] > 126) return false;
    }
    return true;
}

/**
 * @brief Procesa una línea recibida del esclavo
 */
static void process_response(const char *line) {
    if (!is_line_valid(line)) {
        return;
    }

    // IGNORAR ECHOS: Si la línea contiene SYNC (o variantes con basura), es nuestro propio envío
    if (strstr(line, "SYNC") != NULL || strstr(line, "Sy") != NULL) {
        return;
    }

    ESP_LOGD(TAG, "Recibido: %s", line);

    if (strncmp(line, "DATA=", 5) == 0) {
        process_data_response(line);
    }
    else {
        // Solo logueamos como debug si no es DATA= para evitar polución si hay ruido
        ESP_LOGD(TAG, "Línea desconocida: %s", line);
    }
}

// ============================================================================
// TAREAS RTOS
// ============================================================================

/**
 * @brief Tarea de recepción UART - Lee líneas terminadas en \n
 */
static void uart_rx_task(void *pvParameters) {
    char line_buffer[LINE_BUFFER_SIZE];
    size_t line_pos = 0;

    ESP_LOGI(TAG, "Tarea UART RX iniciada (modo ASCII)");

    while (1) {
        uint8_t byte;
        int len = uart_read_bytes(CM_MASTER_UART_PORT, &byte, 1, pdMS_TO_TICKS(10));

        if (len > 0) {
            // Detectar fin de línea
            if (byte == '\n' || byte == '\r') {
                if (line_pos > 0) {
                    line_buffer[line_pos] = '\0';
                    process_response(line_buffer);
                    line_pos = 0;
                }
            }
            // Filtrar ruido: Solo aceptar caracteres ASCII imprimibles
            else if (byte >= 32 && byte <= 126) {
                if (line_pos < LINE_BUFFER_SIZE - 1) {
                    line_buffer[line_pos++] = byte;
                } else {
                    // Si llegamos aquí es que hemos recibido >128 bytes sin un \n
                    // Probablemente ruido persistente o eco masivo.
                    // Solo logueamos una vez por ráfaga para no inundar el monitor
                    ESP_LOGW(TAG, "Buffer RX lleno sin fin de línea. Descartando ráfaga.");
                    line_pos = 0;
                    uart_flush(CM_MASTER_UART_PORT);
                }
            }
            // Si el byte es basura (no ASCII), lo ignoramos silenciosamente para no acumularlo
        }
    }
}

/**
 * @brief Tarea principal del maestro
 *
 * - Envía SYNC cada 100ms con todos los objetivos
 * - Recibe DATA con todos los valores reales
 * - Monitorea timeout de conexión
 */
static void master_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tarea maestro iniciada (protocolo SYNC simplificado)");

    // Esperar 1 segundo para que el esclavo esté completamente inicializado
    vTaskDelay(pdMS_TO_TICKS(1000));

    int64_t last_sync_us = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SYNC_INTERVAL_MS));  // Ciclo de 100ms

        int64_t now_us = esp_timer_get_time();

        // 1. Verificar timeout de conexión
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        int64_t time_since_last_response = now_us - g_last_response_us;
        if (time_since_last_response > (CONNECTION_TIMEOUT_MS * 1000)) {
            if (g_connected) {
                ESP_LOGW(TAG, "Desconectado del esclavo (timeout)");
                g_connected = false;
            }
        }

        // 2. Leer todos los objetivos actuales
        float target_speed = g_target_speed_kmh;
        float target_incline = g_target_incline_pct;
        uint8_t target_fan_head = g_target_head_fan;
        uint8_t target_fan_chest = g_target_chest_fan;
        uint8_t target_wax = g_target_wax_pump;
        bool training_mode = g_training_mode;
        xSemaphoreGive(g_master_mutex);

        // 3. Enviar SYNC cada 100ms (siempre, haya cambios o no)
        if ((now_us - last_sync_us) >= (SYNC_INTERVAL_MS * 1000)) {
            send_sync(target_speed, target_incline, target_fan_head, target_fan_chest, target_wax, training_mode);
            last_sync_us = now_us;
        }
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS (API)
// ============================================================================

esp_err_t cm_master_init(void) {
    ESP_LOGI(TAG, "Inicializando mutex del maestro...");
    if (g_master_mutex == NULL) {
        g_master_mutex = xSemaphoreCreateMutex();
        if (g_master_mutex == NULL) {
            ESP_LOGE(TAG, "Error creando mutex");
            return ESP_FAIL;
        }
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_target_speed_kmh = 0.0f;
    g_target_incline_pct = 0.0f;
    g_target_head_fan = 0;
    g_target_chest_fan = 0;
    g_target_wax_pump = 0;
    g_training_mode = false;
    xSemaphoreGive(g_master_mutex);

    return ESP_OK;
}

esp_err_t cm_master_start(void) {
    ESP_LOGI(TAG, "Iniciando tareas del maestro...");

    // Crear tarea de recepción UART
    BaseType_t ret = xTaskCreate(uart_rx_task, "cm_uart_rx", 4096, NULL, 7, &g_uart_rx_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea UART RX");
        return ESP_FAIL;
    }

    // El mutex ya debe haber sido creado en cm_master_init()

    // Crear tarea principal del maestro
    ret = xTaskCreate(master_task, "cm_master", 4096, NULL, 6, &g_master_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando tarea maestro");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Tareas del maestro creadas correctamente");
    return ESP_OK;
}

esp_err_t cm_master_set_speed(float speed_kmh) {
    if (g_master_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_target_speed_kmh = speed_kmh;
    // g_real_speed_kmh = speed_kmh;  // REMOVED: Optimistic update causaba oscilación cuando sensor=0
    xSemaphoreGive(g_master_mutex);

    static float last_logged_speed = -1.0f;
    if (fabsf(speed_kmh - last_logged_speed) > 0.09f) {
        ESP_LOGI(TAG, "Velocidad objetivo: %.2f km/h", speed_kmh);
        last_logged_speed = speed_kmh;
    }
    return ESP_OK;
}

esp_err_t cm_master_set_incline(float incline_pct) {
    if (g_master_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_target_incline_pct = incline_pct;
    xSemaphoreGive(g_master_mutex);

    static float last_logged_incline = -1.0f;
    if (fabsf(incline_pct - last_logged_incline) > 0.09f) {
        ESP_LOGI(TAG, "Inclinación objetivo: %.1f%%", incline_pct);
        last_logged_incline = incline_pct;
    }
    return ESP_OK;
}

esp_err_t cm_master_calibrate_incline(void) {
    ESP_LOGI(TAG, "Enviando CALIBRATE_INCLINE");
    return send_command_int("CALIBRATE_INCLINE", 1);
}

bool cm_master_is_connected(void) {
    if (g_master_mutex == NULL) {
        return false;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    bool connected = g_connected;
    xSemaphoreGive(g_master_mutex);
    return connected;
}

float cm_master_get_real_speed(void) {
    if (g_master_mutex == NULL) {
        return 0.0f;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    float speed = g_real_speed_kmh;
    xSemaphoreGive(g_master_mutex);
    return speed;
}

float cm_master_get_current_incline(void) {
    if (g_master_mutex == NULL) {
        return 0.0f;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    float incline = g_current_incline_pct;
    xSemaphoreGive(g_master_mutex);
    return incline;
}

esp_err_t cm_master_set_fan(uint8_t fan_id, uint8_t state) {
    if (g_master_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (state > 2) {
        ESP_LOGW(TAG, "Estado de ventilador inválido: %d", state);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    if (fan_id == 0x01) {  // FAN_HEAD
        g_target_head_fan = state;
        ESP_LOGI(TAG, "Ventilador cabeza objetivo: %d", state);
    } else {  // FAN_CHEST
        g_target_chest_fan = state;
        ESP_LOGI(TAG, "Ventilador pecho objetivo: %d", state);
    }
    xSemaphoreGive(g_master_mutex);

    return ESP_OK;
}

uint8_t cm_master_get_head_fan_state(void) {
    if (g_master_mutex == NULL) {
        return 0;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    uint8_t state = g_head_fan_state;
    xSemaphoreGive(g_master_mutex);
    return state;
}

uint8_t cm_master_get_chest_fan_state(void) {
    if (g_master_mutex == NULL) {
        return 0;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    uint8_t state = g_chest_fan_state;
    xSemaphoreGive(g_master_mutex);
    return state;
}

esp_err_t cm_master_set_relay(uint8_t relay_id, uint8_t state) {
    if (g_master_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (relay_id == 0x01) {  // WAX_PUMP
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        g_target_wax_pump = state;
        xSemaphoreGive(g_master_mutex);
        ESP_LOGI(TAG, "Bomba cera objetivo: %d", state);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "ID de relé desconocido: 0x%02X", relay_id);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t cm_master_set_training_mode(bool enabled) {
    if (g_master_mutex == NULL) {
        ESP_LOGW(TAG, "cm_master_set_training_mode: Mutex NO inicializado aún");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_training_mode = enabled;
    xSemaphoreGive(g_master_mutex);

    ESP_LOGI(TAG, "Training mode: %s", enabled ? "ACTIVADO (entrenando)" : "DESACTIVADO (pantalla inicial)");
    return ESP_OK;
}

bool cm_master_get_incline_sensor_fault(void) {
    if (g_master_mutex == NULL) {
        return false;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    bool fault = (g_incline_sensor_fault != 0);
    xSemaphoreGive(g_master_mutex);
    return fault;
}

esp_err_t cm_master_manual_incline_up(void) {
    ESP_LOGI(TAG, "Enviando MAN_UP=1");
    return send_command_int("MAN_UP", 1);
}

esp_err_t cm_master_manual_incline_down(void) {
    ESP_LOGI(TAG, "Enviando MAN_DOWN=1");
    return send_command_int("MAN_DOWN", 1);
}

esp_err_t cm_master_manual_incline_stop(void) {
    ESP_LOGI(TAG, "Enviando MAN_STOP=1");
    return send_command_int("MAN_STOP", 1);
}
