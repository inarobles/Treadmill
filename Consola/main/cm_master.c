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

static const char *TAG = "CM_MASTER";

// ============================================================================
// CONSTANTES
// ============================================================================

#define UART_BUF_SIZE            512
#define LINE_BUFFER_SIZE         128
#define HEARTBEAT_INTERVAL_MS    200  // GET_DATA cada 200ms
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

/** Variables de control (targets) */
static float g_target_speed_kmh = 0.0f;
static float g_target_incline_pct = 0.0f;

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
static esp_err_t send_command(const char *cmd, float value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s=%.2f\n", cmd, value);
    return send_line(buffer);
}

/**
 * @brief Envía comando con valor entero
 */
static esp_err_t send_command_int(const char *cmd, int value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s=%d\n", cmd, value);
    return send_line(buffer);
}

// ============================================================================
// FUNCIONES PRIVADAS - RECEPCIÓN Y PARSING
// ============================================================================

/**
 * @brief Procesa respuesta ACK=OK o ACK=ERROR,<motivo>
 */
static void process_ack_response(const char *line) {
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_last_response_us = esp_timer_get_time();
    g_connected = true;
    xSemaphoreGive(g_master_mutex);

    if (strcmp(line, "ACK=OK") == 0) {
        ESP_LOGD(TAG, "ACK recibido: OK");
    } else if (strncmp(line, "ACK=ERROR,", 10) == 0) {
        ESP_LOGW(TAG, "ACK recibido: %s", line + 4);
    }
}

/**
 * @brief Procesa respuesta DATA=<speed>,<incline>,<vfd_freq>,<vfd_fault>,<fan_head>,<fan_chest>
 */
static void process_data_response(const char *line) {
    float speed, incline, vfd_freq;
    int vfd_fault, fan_head, fan_chest;

    int parsed = sscanf(line, "DATA=%f,%f,%f,%d,%d,%d",
                        &speed, &incline, &vfd_freq,
                        &vfd_fault, &fan_head, &fan_chest);

    if (parsed == 6) {
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        g_real_speed_kmh = speed;
        g_current_incline_pct = incline;
        g_vfd_freq_hz = vfd_freq;
        g_vfd_fault = (uint8_t)vfd_fault;
        g_head_fan_state = (uint8_t)fan_head;
        g_chest_fan_state = (uint8_t)fan_chest;
        g_last_response_us = esp_timer_get_time();
        g_connected = true;
        xSemaphoreGive(g_master_mutex);

        ESP_LOGD(TAG, "DATA: speed=%.2f incline=%.1f vfd_freq=%.2f fault=%d fans=%d,%d",
                 speed, incline, vfd_freq, vfd_fault, fan_head, fan_chest);
    } else {
        ESP_LOGW(TAG, "Error al parsear DATA: %s", line);
    }
}

/**
 * @brief Procesa una línea recibida del esclavo
 */
static void process_response(const char *line) {
    ESP_LOGD(TAG, "Recibido: %s", line);

    if (strncmp(line, "ACK=", 4) == 0) {
        process_ack_response(line);
    }
    else if (strncmp(line, "DATA=", 5) == 0) {
        process_data_response(line);
    }
    else {
        ESP_LOGW(TAG, "Línea desconocida: %s", line);
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
        int len = uart_read_bytes(CM_MASTER_UART_PORT, &byte, 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            // Detectar fin de línea
            if (byte == '\n' || byte == '\r') {
                if (line_pos > 0) {
                    // Terminar string y procesar respuesta
                    line_buffer[line_pos] = '\0';
                    process_response(line_buffer);
                    line_pos = 0;
                }
            }
            // Acumular caracteres (ignorar \r adicionales)
            else if (byte >= 32 && byte < 127) {  // Solo caracteres imprimibles ASCII
                if (line_pos < LINE_BUFFER_SIZE - 1) {
                    line_buffer[line_pos++] = byte;
                } else {
                    ESP_LOGW(TAG, "Línea demasiado larga, descartando");
                    line_pos = 0;
                }
            }
        }
    }
}

/**
 * @brief Tarea principal del maestro
 *
 * - Envía GET_DATA cada 500ms (heartbeat)
 * - Envía SET_SPEED cuando cambia el target
 * - Envía SET_INCLINE cuando cambia el target
 * - Monitorea timeout de conexión
 */
static void master_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tarea maestro iniciada");

    // Esperar 1 segundo para que el esclavo esté completamente inicializado
    vTaskDelay(pdMS_TO_TICKS(1000));

    float last_sent_speed = -1.0f;
    float last_sent_incline = -1.0f;
    int64_t last_heartbeat_us = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));  // Ciclo de 50ms

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
        xSemaphoreGive(g_master_mutex);

        // 2. Enviar SET_SPEED si cambió el target
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        float target_speed = g_target_speed_kmh;
        xSemaphoreGive(g_master_mutex);

        if (target_speed != last_sent_speed) {
            send_command("SET_SPEED", target_speed);
            last_sent_speed = target_speed;
        }

        // 3. Enviar SET_INCLINE si cambió el target
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        float target_incline = g_target_incline_pct;
        xSemaphoreGive(g_master_mutex);

        if (target_incline != last_sent_incline) {
            send_command("SET_INCLINE", target_incline);
            last_sent_incline = target_incline;
        }

        // 4. Enviar heartbeat GET_DATA cada 200ms
        if ((now_us - last_heartbeat_us) >= (HEARTBEAT_INTERVAL_MS * 1000)) {
            send_command_int("GET_DATA", 1);
            last_heartbeat_us = now_us;
        }
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS (API)
// ============================================================================

esp_err_t cm_master_init(void) {
    ESP_LOGI(TAG, "Inicializando CM Master (Protocolo ASCII)...");

    // Crear mutex
    g_master_mutex = xSemaphoreCreateMutex();
    if (g_master_mutex == NULL) {
        ESP_LOGE(TAG, "Error creando mutex");
        return ESP_FAIL;
    }

    // Configurar UART
    uart_config_t uart_config = {
        .baud_rate = CM_MASTER_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(CM_MASTER_UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error instalando driver UART: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(CM_MASTER_UART_PORT, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando parámetros UART: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(CM_MASTER_UART_PORT,
                       CM_MASTER_TX_PIN,
                       CM_MASTER_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando pines UART: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UART%d configurado: %d baud, TX=%d, RX=%d",
             CM_MASTER_UART_PORT, CM_MASTER_BAUD_RATE,
             CM_MASTER_TX_PIN, CM_MASTER_RX_PIN);

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
    xSemaphoreGive(g_master_mutex);

    ESP_LOGI(TAG, "Velocidad objetivo: %.2f km/h", speed_kmh);
    return ESP_OK;
}

esp_err_t cm_master_set_incline(float incline_pct) {
    if (g_master_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;  // No inicializado aún
    }
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_target_incline_pct = incline_pct;
    xSemaphoreGive(g_master_mutex);

    ESP_LOGI(TAG, "Inclinación objetivo: %.1f%%", incline_pct);
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
    if (state > 2) {
        ESP_LOGW(TAG, "Estado de ventilador inválido: %d", state);
        return ESP_ERR_INVALID_ARG;
    }

    const char *cmd = (fan_id == 0x01) ? "SET_FAN_HEAD" : "SET_FAN_CHEST";
    ESP_LOGI(TAG, "%s=%d", cmd, state);
    return send_command_int(cmd, state);
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
    if (relay_id == 0x01) {  // WAX_PUMP
        ESP_LOGI(TAG, "CMD_WAX=%d", state);
        return send_command_int("CMD_WAX", state);
    }
    ESP_LOGW(TAG, "ID de relé desconocido: 0x%02X", relay_id);
    return ESP_ERR_INVALID_ARG;
}
