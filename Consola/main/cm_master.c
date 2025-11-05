/**
 * @file cm_master.c
 * @brief Implementación del módulo maestro CM Protocol
 *
 * FASE 9: Sistema asíncrono de timeout y retry
 * - Cola de comandos pendientes
 * - Timeout de 100ms para ACK/NAK
 * - Reintentos automáticos (máx 3)
 * - Detección de desconexión
 */

#include "cm_master.h"
#include "cm_protocol.h"
#include "cm_types.h"
#include "cm_frame.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CM_MASTER";

// ============================================================================
// CONSTANTES DE TIMEOUT Y RETRY
// ============================================================================

#define CM_MASTER_TIMEOUT_MS      100   // Timeout para esperar ACK/NAK/RSP
#define CM_MASTER_MAX_RETRIES     3     // Número máximo de reintentos
#define CM_MASTER_WATCHDOG_MS     50    // Intervalo de verificación de la cola
#define CM_MASTER_CONN_TIMEOUT_MS 1000  // Sin respuestas en 1s = desconectado
#define CM_MASTER_HEARTBEAT_MS    300   // Intervalo de heartbeat/polling
#define MAX_PENDING_CMDS          4     // Máximo 4 comandos pendientes simultáneos

// ============================================================================
// ESTRUCTURAS DE DATOS
// ============================================================================

/**
 * @brief Comando pendiente de ACK/NAK o respuesta
 */
typedef struct {
    cm_frame_t frame;           // Trama a enviar
    uint8_t retry_count;        // Intentos realizados
    int64_t timestamp_us;       // Timestamp del último envío (esp_timer_get_time)
    bool active;                // true si el slot está ocupado
    bool waiting_ack;           // true si espera ACK/NAK, false si espera RSP
} pending_cmd_t;

// ============================================================================
// VARIABLES PRIVADAS
// ============================================================================

/** Número de secuencia global (se incrementa con cada comando nuevo) */
static uint8_t g_seq = 0;

/** Estado de conexión */
static bool g_connected = false;

/** Timestamp de la última respuesta válida recibida */
static int64_t g_last_response_us = 0;

/** Última velocidad real recibida del esclavo */
static float g_real_speed_kmh = 0.0f;

/** Estado del ventilador de cabeza recibido del esclavo (0/1/2) */
static uint8_t g_head_fan_state = 0;

/** Estado del ventilador de pecho recibido del esclavo (0/1/2) */
static uint8_t g_chest_fan_state = 0;

/** Última inclinación actual estimada recibida del esclavo */
static float g_current_incline_pct = 0.0f;

/** Velocidad objetivo solicitada por la UI (km/h) */
static float g_target_speed_kmh = 0.0f;

/** Inclinación objetivo solicitada por la UI (%) */
static float g_target_incline_pct = 0.0f;

/** Última velocidad enviada al esclavo */
static float g_last_sent_speed_kmh = 0.0f;

/** Última inclinación enviada al esclavo */
static float g_last_sent_incline_pct = 0.0f;

/** Flag: esperando ACK de SET_SPEED */
static bool g_waiting_for_speed_ack = false;

/** Flag: esperando ACK de SET_INCLINE */
static bool g_waiting_for_incline_ack = false;

/** Mutex para proteger variables compartidas */
static SemaphoreHandle_t g_master_mutex = NULL;

/** Handle de la tarea del maestro */
static TaskHandle_t g_master_task_handle = NULL;

/** Handle de la tarea de recepción UART */
static TaskHandle_t g_rx_task_handle = NULL;

/** Handle de la tarea de watchdog de comandos */
static TaskHandle_t g_watchdog_task_handle = NULL;

/** Cola de comandos pendientes */
static pending_cmd_t g_pending_cmds[MAX_PENDING_CMDS];

/** Mutex para proteger la cola de comandos pendientes */
static SemaphoreHandle_t g_pending_mutex = NULL;

// ============================================================================
// FUNCIONES PRIVADAS - GESTIÓN DE COLA DE PENDIENTES
// ============================================================================

/**
 * @brief Busca un slot libre en la cola de comandos pendientes
 * @return Índice del slot libre, o -1 si está llena
 */
static int find_free_slot(void) {
    for (int i = 0; i < MAX_PENDING_CMDS; i++) {
        if (!g_pending_cmds[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Busca un comando pendiente por su número de secuencia
 * @return Índice del comando, o -1 si no se encuentra
 */
static int find_pending_by_seq(uint8_t seq) {
    for (int i = 0; i < MAX_PENDING_CMDS; i++) {
        if (g_pending_cmds[i].active && g_pending_cmds[i].frame.seq == seq) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Elimina un comando de la cola de pendientes
 */
static void remove_pending_cmd(int index) {
    if (index >= 0 && index < MAX_PENDING_CMDS) {
        g_pending_cmds[index].active = false;
    }
}

// ============================================================================
// FUNCIONES PRIVADAS - ENVÍO DE TRAMAS
// ============================================================================

/**
 * @brief Envía una trama por UART (función de bajo nivel)
 */
static esp_err_t send_frame_raw(const cm_frame_t *frame) {
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    // Construir trama física (con SOF, stuffing y CRC)
    uint8_t tx_buffer[CM_MAX_STUFFED_SIZE];
    size_t tx_len = cm_build_frame(frame, tx_buffer, sizeof(tx_buffer));

    if (tx_len == 0) {
        ESP_LOGE(TAG, "Failed to build frame");
        return ESP_FAIL;
    }

    // Enviar por UART
    int written = uart_write_bytes(CM_MASTER_UART_PORT, tx_buffer, tx_len);

    if (written < 0) {
        ESP_LOGE(TAG, "UART write failed");
        return ESP_FAIL;
    }

    // Esperar a que se envíe completamente
    uart_wait_tx_done(CM_MASTER_UART_PORT, pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Sent frame: CMD=0x%02X, SEQ=%d, LEN=%d", frame->cmd, frame->seq, frame->len);

    return ESP_OK;
}

/**
 * @brief Envía un comando y lo agrega a la cola de pendientes
 * @param frame Trama a enviar
 * @param wait_ack true si espera ACK/NAK, false si espera RSP directa
 * @return ESP_OK si se envió correctamente, ESP_FAIL si la cola está llena
 */
static esp_err_t send_frame_async(const cm_frame_t *frame, bool wait_ack) {
    xSemaphoreTake(g_pending_mutex, portMAX_DELAY);

    // Buscar slot libre
    int slot = find_free_slot();
    if (slot < 0) {
        xSemaphoreGive(g_pending_mutex);
        ESP_LOGW(TAG, "Pending queue full! Cannot send CMD=0x%02X", frame->cmd);
        return ESP_FAIL;
    }

    // Agregar a cola de pendientes
    memcpy(&g_pending_cmds[slot].frame, frame, sizeof(cm_frame_t));
    g_pending_cmds[slot].retry_count = 0;
    g_pending_cmds[slot].timestamp_us = esp_timer_get_time();
    g_pending_cmds[slot].active = true;
    g_pending_cmds[slot].waiting_ack = wait_ack;

    xSemaphoreGive(g_pending_mutex);

    // Enviar inmediatamente
    return send_frame_raw(frame);
}

/**
 * @brief Envía comando GET_STATUS (heartbeat)
 */
static esp_err_t send_get_status(void) {
    cm_frame_t frame = {
        .len = 0,
        .seq = g_seq++,
        .cmd = CM_CMD_GET_STATUS,
    };

    // GET_STATUS espera RSP_STATUS (no ACK)
    return send_frame_async(&frame, false);
}

static esp_err_t send_get_sensor_speed(void) {
    cm_frame_t frame = {
        .len = 0,
        .seq = g_seq++,
        .cmd = CM_CMD_GET_SENSOR_SPEED,
    };

    // GET_SENSOR_SPEED espera RSP_SENSOR_SPEED (no ACK)
    return send_frame_async(&frame, false);
}

static esp_err_t send_get_incline_position(void) {
    cm_frame_t frame = {
        .len = 0,
        .seq = g_seq++,
        .cmd = CM_CMD_GET_INCLINE_POSITION,
    };

    // GET_INCLINE_POSITION espera RSP_INCLINE_POSITION (no ACK)
    return send_frame_async(&frame, false);
}

static esp_err_t send_get_fan_state(void) {
    cm_frame_t frame = {
        .len = 0,
        .seq = g_seq++,
        .cmd = CM_CMD_GET_FAN_STATE,
    };

    // GET_FAN_STATE espera RSP_FAN_STATE (no ACK)
    return send_frame_async(&frame, false);
}

/**
 * @brief Envía comando SET_SPEED desde el heartbeat
 */
static esp_err_t send_set_speed_internal(float speed_kmh) {
    uint16_t speed_x100 = (uint16_t)(speed_kmh * 100.0f);

    // Limitar a rango válido
    if (speed_x100 > 1950) {
        speed_x100 = 1950;
    }

    cm_frame_t frame = {
        .len = 2,
        .seq = g_seq++,
        .cmd = CM_CMD_SET_SPEED,
    };
    frame.payload[0] = (speed_x100 >> 8) & 0xFF;
    frame.payload[1] = speed_x100 & 0xFF;

    ESP_LOGI(TAG, "Sending SET_SPEED: %.1f km/h (0x%04X)", speed_kmh, speed_x100);

    // SET_SPEED espera ACK
    return send_frame_async(&frame, true);
}

/**
 * @brief Envía comando SET_INCLINE desde el heartbeat
 */
static esp_err_t send_set_incline_internal(float incline_pct) {
    uint16_t incline_x10 = (uint16_t)(incline_pct * 10.0f);

    // Limitar a rango válido (0-15%)
    if (incline_x10 > 150) {
        incline_x10 = 150;
    }

    cm_frame_t frame = {
        .len = 2,
        .seq = g_seq++,
        .cmd = CM_CMD_SET_INCLINE,
    };
    frame.payload[0] = (incline_x10 >> 8) & 0xFF;
    frame.payload[1] = incline_x10 & 0xFF;

    ESP_LOGI(TAG, "Sending SET_INCLINE: %.1f %% (0x%04X)", incline_pct, incline_x10);

    // SET_INCLINE espera ACK
    return send_frame_async(&frame, true);
}

// ============================================================================
// FUNCIONES PRIVADAS - PROCESAMIENTO DE RESPUESTAS
// ============================================================================

/**
 * @brief Procesa una trama RSP_STATUS recibida del esclavo
 */
static void process_rsp_status(const cm_frame_t *frame) {
    if (frame->len != 1) {
        ESP_LOGW(TAG, "RSP_STATUS con longitud incorrecta: %d (esperado 1)", frame->len);
        return;
    }

    uint8_t status_byte = frame->payload[0];

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_connected = true;
    g_last_response_us = esp_timer_get_time();
    // No actualizamos speed, incline ni fans aquí - solo el bitmap de estado
    xSemaphoreGive(g_master_mutex);

    // Eliminar comando de la cola de pendientes
    xSemaphoreTake(g_pending_mutex, portMAX_DELAY);
    int idx = find_pending_by_seq(frame->seq);
    if (idx >= 0) {
        remove_pending_cmd(idx);
        ESP_LOGD(TAG, "RSP_STATUS received, removed SEQ=%d from pending queue", frame->seq);
    }
    xSemaphoreGive(g_pending_mutex);

    ESP_LOGI(TAG, "RSP_STATUS: status=0x%02X (VFD: %s)",
             status_byte, (status_byte & 0x01) ? "FAULT" : "OK");
}

/**
 * @brief Procesa un ACK recibido
 */
static void process_ack(const cm_frame_t *frame) {
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_connected = true;
    g_last_response_us = esp_timer_get_time();
    xSemaphoreGive(g_master_mutex);

    // Verificar qué comando recibió el ACK para liberar el flag correspondiente
    xSemaphoreTake(g_pending_mutex, portMAX_DELAY);
    int idx = find_pending_by_seq(frame->seq);
    if (idx >= 0) {
        uint8_t cmd = g_pending_cmds[idx].frame.cmd;
        remove_pending_cmd(idx);
        ESP_LOGI(TAG, "ACK received for SEQ=%d, removed from pending queue", frame->seq);

        // Liberar el flag de espera correspondiente
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        if (cmd == CM_CMD_SET_SPEED) {
            g_waiting_for_speed_ack = false;
            ESP_LOGD(TAG, "Speed ACK flag cleared");
        } else if (cmd == CM_CMD_SET_INCLINE) {
            g_waiting_for_incline_ack = false;
            ESP_LOGD(TAG, "Incline ACK flag cleared");
        }
        xSemaphoreGive(g_master_mutex);
    }
    xSemaphoreGive(g_pending_mutex);
}

/**
 * @brief Procesa un NAK recibido
 */
static void process_nak(const cm_frame_t *frame) {
    ESP_LOGW(TAG, "NAK received for SEQ=%d", frame->seq);

    // NAK = slave rechazó el comando, NO reintentamos
    xSemaphoreTake(g_pending_mutex, portMAX_DELAY);
    int idx = find_pending_by_seq(frame->seq);
    if (idx >= 0) {
        uint8_t cmd = g_pending_cmds[idx].frame.cmd;
        remove_pending_cmd(idx);
        ESP_LOGW(TAG, "Command SEQ=%d rejected by slave (NAK), removed from queue", frame->seq);

        // Liberar el flag de espera correspondiente (permite reintento en siguiente ciclo)
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        if (cmd == CM_CMD_SET_SPEED) {
            g_waiting_for_speed_ack = false;
            ESP_LOGD(TAG, "Speed ACK flag cleared after NAK");
        } else if (cmd == CM_CMD_SET_INCLINE) {
            g_waiting_for_incline_ack = false;
            ESP_LOGD(TAG, "Incline ACK flag cleared after NAK");
        }
        xSemaphoreGive(g_master_mutex);
    }
    xSemaphoreGive(g_pending_mutex);
}

/**
 * @brief Procesa una trama RSP_SENSOR_SPEED recibida del esclavo
 */
static void process_rsp_sensor_speed(const cm_frame_t *frame) {
    if (frame->len != 2) {
        ESP_LOGW(TAG, "RSP_SENSOR_SPEED con longitud incorrecta: %d (esperado 2)", frame->len);
        return;
    }

    uint16_t speed_x100 = ((uint16_t)frame->payload[0] << 8) | frame->payload[1];
    float real_speed = (float)speed_x100 / 100.0f;

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_connected = true;
    g_last_response_us = esp_timer_get_time();
    g_real_speed_kmh = real_speed;
    xSemaphoreGive(g_master_mutex);

    // Eliminar comando de la cola de pendientes
    xSemaphoreTake(g_pending_mutex, portMAX_DELAY);
    int idx = find_pending_by_seq(frame->seq);
    if (idx >= 0) {
        remove_pending_cmd(idx);
        ESP_LOGD(TAG, "RSP_SENSOR_SPEED received, removed SEQ=%d from pending queue", frame->seq);
    }
    xSemaphoreGive(g_pending_mutex);

    ESP_LOGI(TAG, "RSP_SENSOR_SPEED: speed=%.2f km/h", real_speed);
}

/**
 * @brief Procesa una trama RSP_INCLINE_POSITION recibida del esclavo
 */
static void process_rsp_incline_position(const cm_frame_t *frame) {
    if (frame->len != 2) {
        ESP_LOGW(TAG, "RSP_INCLINE_POSITION con longitud incorrecta: %d (esperado 2)", frame->len);
        return;
    }

    uint16_t incline_x10 = ((uint16_t)frame->payload[0] << 8) | frame->payload[1];
    float real_incline = (float)incline_x10 / 10.0f;

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_connected = true;
    g_last_response_us = esp_timer_get_time();
    g_current_incline_pct = real_incline;
    xSemaphoreGive(g_master_mutex);

    // Eliminar comando de la cola de pendientes
    xSemaphoreTake(g_pending_mutex, portMAX_DELAY);
    int idx = find_pending_by_seq(frame->seq);
    if (idx >= 0) {
        remove_pending_cmd(idx);
        ESP_LOGD(TAG, "RSP_INCLINE_POSITION received, removed SEQ=%d from pending queue", frame->seq);
    }
    xSemaphoreGive(g_pending_mutex);

    ESP_LOGI(TAG, "RSP_INCLINE_POSITION: incline=%.1f %%", real_incline);
}

/**
 * @brief Procesa una trama RSP_FAN_STATE recibida del esclavo
 */
static void process_rsp_fan_state(const cm_frame_t *frame) {
    if (frame->len != 2) {
        ESP_LOGW(TAG, "RSP_FAN_STATE con longitud incorrecta: %d (esperado 2)", frame->len);
        return;
    }

    uint8_t head_fan = frame->payload[0];
    uint8_t chest_fan = frame->payload[1];

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_connected = true;
    g_last_response_us = esp_timer_get_time();
    g_head_fan_state = head_fan;
    g_chest_fan_state = chest_fan;
    xSemaphoreGive(g_master_mutex);

    // Eliminar comando de la cola de pendientes
    xSemaphoreTake(g_pending_mutex, portMAX_DELAY);
    int idx = find_pending_by_seq(frame->seq);
    if (idx >= 0) {
        remove_pending_cmd(idx);
        ESP_LOGD(TAG, "RSP_FAN_STATE received, removed SEQ=%d from pending queue", frame->seq);
    }
    xSemaphoreGive(g_pending_mutex);

    ESP_LOGI(TAG, "RSP_FAN_STATE: head=%d, chest=%d", head_fan, chest_fan);
}

// ============================================================================
// TAREA DE RECEPCIÓN UART
// ============================================================================

/**
 * @brief Tarea de recepción UART - parsea tramas byte por byte
 */
static void uart_rx_task(void *arg) {
    ESP_LOGI(TAG, "UART RX task started");

    uint8_t rx_buffer[CM_MAX_FRAME_SIZE * 2];
    size_t rx_index = 0;
    bool in_frame = false;

    while (1) {
        uint8_t byte;
        int len = uart_read_bytes(CM_MASTER_UART_PORT, &byte, 1, pdMS_TO_TICKS(100));

        if (len <= 0) {
            continue;
        }

        // Detectar SOF
        if (byte == CM_SOF) {
            in_frame = true;
            rx_index = 0;
            rx_buffer[rx_index++] = byte;
            continue;
        }

        // Si estamos en una trama, acumular bytes
        if (in_frame) {
            rx_buffer[rx_index++] = byte;

            // Evitar desbordamiento
            if (rx_index >= sizeof(rx_buffer)) {
                ESP_LOGW(TAG, "RX buffer overflow, resetting");
                in_frame = false;
                rx_index = 0;
                continue;
            }

            // Intentar parsear si tenemos suficientes bytes
            if (rx_index >= 6) {
                cm_frame_t frame;
                if (cm_parse_frame(rx_buffer, rx_index, &frame)) {
                    // Trama válida
                    ESP_LOGI(TAG, "Valid frame received: CMD=0x%02X, SEQ=%d", frame.cmd, frame.seq);

                    // Procesar según comando
                    switch (frame.cmd) {
                        case CM_RSP_STATUS:
                            process_rsp_status(&frame);
                            break;
                        case CM_RSP_ACK:
                            process_ack(&frame);
                            break;
                        case CM_RSP_NAK:
                            process_nak(&frame);
                            break;
                        case CM_RSP_SENSOR_SPEED:
                            process_rsp_sensor_speed(&frame);
                            break;
                        case CM_RSP_INCLINE_POSITION:
                            process_rsp_incline_position(&frame);
                            break;
                        case CM_RSP_FAN_STATE:
                            process_rsp_fan_state(&frame);
                            break;
                        default:
                            ESP_LOGW(TAG, "Unexpected command: 0x%02X", frame.cmd);
                            break;
                    }

                    // Reiniciar para siguiente trama
                    in_frame = false;
                    rx_index = 0;
                }
            }
        }
    }
}

// ============================================================================
// TAREA DE WATCHDOG DE COMANDOS
// ============================================================================

/**
 * @brief Tarea de watchdog que verifica timeouts y reintentos
 */
static void cmd_watchdog_task(void *arg) {
    ESP_LOGI(TAG, "Command watchdog task started (timeout=%dms, interval=%dms)",
             CM_MASTER_TIMEOUT_MS, CM_MASTER_WATCHDOG_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CM_MASTER_WATCHDOG_MS));

        int64_t now_us = esp_timer_get_time();

        xSemaphoreTake(g_pending_mutex, portMAX_DELAY);

        // Revisar cada comando pendiente
        for (int i = 0; i < MAX_PENDING_CMDS; i++) {
            if (!g_pending_cmds[i].active) {
                continue;
            }

            int64_t elapsed_us = now_us - g_pending_cmds[i].timestamp_us;
            int64_t elapsed_ms = elapsed_us / 1000;

            // ¿Ha pasado el timeout?
            if (elapsed_ms > CM_MASTER_TIMEOUT_MS) {
                pending_cmd_t *cmd = &g_pending_cmds[i];

                // ¿Aún tenemos reintentos disponibles?
                if (cmd->retry_count < CM_MASTER_MAX_RETRIES) {
                    cmd->retry_count++;
                    cmd->timestamp_us = now_us;

                    ESP_LOGW(TAG, "Timeout for CMD=0x%02X SEQ=%d (retry %d/%d)",
                             cmd->frame.cmd, cmd->frame.seq,
                             cmd->retry_count, CM_MASTER_MAX_RETRIES);

                    // Reenviar comando
                    send_frame_raw(&cmd->frame);
                } else {
                    // Máximo de reintentos alcanzado
                    ESP_LOGE(TAG, "Max retries reached for CMD=0x%02X SEQ=%d - giving up",
                             cmd->frame.cmd, cmd->frame.seq);

                    // Eliminar de la cola
                    remove_pending_cmd(i);

                    // Marcar como desconectado
                    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
                    g_connected = false;
                    xSemaphoreGive(g_master_mutex);
                }
            }
        }

        xSemaphoreGive(g_pending_mutex);

        // Verificar timeout de conexión (si hace >1s que no recibimos nada)
        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
        int64_t since_last_response_ms = (now_us - g_last_response_us) / 1000;
        if (since_last_response_ms > CM_MASTER_CONN_TIMEOUT_MS && g_connected) {
            ESP_LOGW(TAG, "No responses for %lld ms - marking as disconnected", since_last_response_ms);
            g_connected = false;
        }
        xSemaphoreGive(g_master_mutex);
    }
}

// ============================================================================
// TAREA DEL MAESTRO
// ============================================================================

/**
 * @brief Tarea principal del maestro - envía heartbeats periódicos
 */
static void cm_master_task(void *arg) {
    ESP_LOGI(TAG, "Master task started - heartbeat every %d ms", CM_MASTER_HEARTBEAT_MS);

    TickType_t last_heartbeat = xTaskGetTickCount();

    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool command_sent = false;

        // Enviar heartbeat si ha pasado el intervalo
        if ((now - last_heartbeat) >= pdMS_TO_TICKS(CM_MASTER_HEARTBEAT_MS)) {

            // --- PASO 1: PRIORIZAR COMANDOS SET (Acciones del Usuario) ---

            // Comprobar si el objetivo de inclinación ha cambiado Y si no estamos esperando un ACK
            xSemaphoreTake(g_master_mutex, portMAX_DELAY);
            float target_incline = g_target_incline_pct;
            float last_sent_incline = g_last_sent_incline_pct;
            bool waiting_incline_ack = g_waiting_for_incline_ack;
            xSemaphoreGive(g_master_mutex);

            if (target_incline != last_sent_incline && !waiting_incline_ack) {
                if (send_set_incline_internal(target_incline) == ESP_OK) {
                    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
                    g_last_sent_incline_pct = target_incline;
                    g_waiting_for_incline_ack = true;
                    xSemaphoreGive(g_master_mutex);
                    command_sent = true;
                    vTaskDelay(pdMS_TO_TICKS(50)); // Pausa breve tras enviar SET
                }
            }

            // Comprobar si el objetivo de velocidad ha cambiado Y si no estamos esperando un ACK
            if (!command_sent) {
                xSemaphoreTake(g_master_mutex, portMAX_DELAY);
                float target_speed = g_target_speed_kmh;
                float last_sent_speed = g_last_sent_speed_kmh;
                bool waiting_speed_ack = g_waiting_for_speed_ack;
                xSemaphoreGive(g_master_mutex);

                if (target_speed != last_sent_speed && !waiting_speed_ack) {
                    if (send_set_speed_internal(target_speed) == ESP_OK) {
                        xSemaphoreTake(g_master_mutex, portMAX_DELAY);
                        g_last_sent_speed_kmh = target_speed;
                        g_waiting_for_speed_ack = true;
                        xSemaphoreGive(g_master_mutex);
                        command_sent = true;
                        vTaskDelay(pdMS_TO_TICKS(50)); // Pausa breve tras enviar SET
                    }
                }
            }

            // --- PASO 2: POLLING GET (Heartbeat de Fondo) ---

            // Verificar si hay SET pendientes de ACK
            xSemaphoreTake(g_master_mutex, portMAX_DELAY);
            bool has_pending_set = g_waiting_for_incline_ack || g_waiting_for_speed_ack;
            xSemaphoreGive(g_master_mutex);

            if (has_pending_set) {
                // Si hay SET pendiente, solo polling crítico (status VFD)
                send_get_status();
                vTaskDelay(pdMS_TO_TICKS(CM_MASTER_HEARTBEAT_MS));
            } else {
                // Polling completo si no hay SET pendiente
                send_get_status();
                vTaskDelay(pdMS_TO_TICKS(10));
                send_get_sensor_speed();
                vTaskDelay(pdMS_TO_TICKS(10));
                send_get_incline_position();
                vTaskDelay(pdMS_TO_TICKS(10));
                send_get_fan_state();
                vTaskDelay(pdMS_TO_TICKS(CM_MASTER_HEARTBEAT_MS - 30));
            }

            last_heartbeat = now;
        } else {
            // Dormir para no saturar CPU
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// ============================================================================
// FUNCIONES PÚBLICAS
// ============================================================================

esp_err_t cm_master_init(void) {
    ESP_LOGI(TAG, "Initializing CM Protocol Master...");

    // Crear mutexes
    g_master_mutex = xSemaphoreCreateMutex();
    if (g_master_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create master mutex");
        return ESP_FAIL;
    }

    g_pending_mutex = xSemaphoreCreateMutex();
    if (g_pending_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create pending mutex");
        return ESP_FAIL;
    }

    // Configurar UART
    uart_config_t uart_config = {
        .baud_rate = CM_MASTER_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(CM_MASTER_UART_PORT, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CM_MASTER_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CM_MASTER_UART_PORT, CM_MASTER_TX_PIN, CM_MASTER_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART%d configured: %d baud, TX=%d, RX=%d",
             CM_MASTER_UART_PORT, CM_MASTER_BAUD_RATE,
             CM_MASTER_TX_PIN, CM_MASTER_RX_PIN);

    // Inicializar variables
    g_seq = 0;
    g_connected = false;
    g_last_response_us = esp_timer_get_time();
    g_real_speed_kmh = 0.0f;
    g_head_fan_state = 0;
    g_chest_fan_state = 0;

    // Inicializar cola de comandos pendientes
    memset(g_pending_cmds, 0, sizeof(g_pending_cmds));

    ESP_LOGI(TAG, "CM Protocol Master initialized (timeout=%dms, max_retries=%d)",
             CM_MASTER_TIMEOUT_MS, CM_MASTER_MAX_RETRIES);
    return ESP_OK;
}

esp_err_t cm_master_start(void) {
    if (g_master_task_handle != NULL || g_rx_task_handle != NULL || g_watchdog_task_handle != NULL) {
        ESP_LOGW(TAG, "Master tasks already running");
        return ESP_OK;
    }

    // Crear tarea de recepción UART
    BaseType_t ret = xTaskCreate(
        uart_rx_task,
        "cm_uart_rx",
        4096,
        NULL,
        10,  // Prioridad alta
        &g_rx_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART RX task");
        return ESP_FAIL;
    }

    // Crear tarea de watchdog de comandos
    ret = xTaskCreate(
        cmd_watchdog_task,
        "cm_cmd_watchdog",
        3072,
        NULL,
        9,  // Prioridad alta (menor que RX)
        &g_watchdog_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create watchdog task");
        return ESP_FAIL;
    }

    // Crear tarea del maestro
    ret = xTaskCreate(
        cm_master_task,
        "cm_master_task",
        4096,
        NULL,
        8,  // Prioridad media
        &g_master_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create master task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Master tasks started (TX + RX + Watchdog)");
    return ESP_OK;
}

esp_err_t cm_master_set_speed(float speed_kmh) {
    // Limitar a rango válido
    if (speed_kmh > 19.5f) {
        speed_kmh = 19.5f;
    }
    if (speed_kmh < 0.0f) {
        speed_kmh = 0.0f;
    }

    // Solo actualizar la variable objetivo
    // El heartbeat se encargará de enviar el comando
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_target_speed_kmh = speed_kmh;
    xSemaphoreGive(g_master_mutex);

    ESP_LOGD(TAG, "Target speed updated: %.1f km/h", speed_kmh);
    return ESP_OK;
}

esp_err_t cm_master_set_incline(float incline_pct) {
    // Limitar a rango válido (0-15%)
    if (incline_pct > 15.0f) {
        incline_pct = 15.0f;
    }
    if (incline_pct < 0.0f) {
        incline_pct = 0.0f;
    }

    // Solo actualizar la variable objetivo
    // El heartbeat se encargará de enviar el comando
    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    g_target_incline_pct = incline_pct;
    xSemaphoreGive(g_master_mutex);

    ESP_LOGD(TAG, "Target incline updated: %.1f %%", incline_pct);
    return ESP_OK;
}

esp_err_t cm_master_calibrate_incline(void) {
    cm_frame_t frame = {
        .len = 0,
        .seq = g_seq++,
        .cmd = CM_CMD_CALIBRATE_INCLINE,
    };

    ESP_LOGI(TAG, "Sending CALIBRATE_INCLINE");

    // CALIBRATE_INCLINE espera ACK
    return send_frame_async(&frame, true);
}

bool cm_master_is_connected(void) {
    if (g_master_mutex == NULL) {
        return false;
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    bool connected = g_connected;
    xSemaphoreGive(g_master_mutex);

    return connected;
}

float cm_master_get_real_speed(void) {
    if (g_master_mutex == NULL) {
        return 0.0f;
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    float speed = g_real_speed_kmh;
    xSemaphoreGive(g_master_mutex);

    return speed;
}

esp_err_t cm_master_set_fan(uint8_t fan_id, uint8_t state) {
    // Validar parámetros
    if ((fan_id != 0x01 && fan_id != 0x02) || state > 2) {
        ESP_LOGE(TAG, "Invalid FAN parameters: ID=0x%02X, state=%d", fan_id, state);
        return ESP_ERR_INVALID_ARG;
    }

    cm_frame_t frame = {
        .len = 2,
        .seq = g_seq++,
        .cmd = CM_CMD_SET_FAN_STATE,
    };
    frame.payload[0] = fan_id;
    frame.payload[1] = state;

    const char *fan_name = (fan_id == 0x01) ? "HEAD" : "CHEST";
    ESP_LOGI(TAG, "Sending SET_FAN_STATE: %s fan = %d", fan_name, state);

    // SET_FAN_STATE espera ACK
    return send_frame_async(&frame, true);
}

uint8_t cm_master_get_head_fan_state(void) {
    if (g_master_mutex == NULL) {
        return 0;
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    uint8_t state = g_head_fan_state;
    xSemaphoreGive(g_master_mutex);

    return state;
}

uint8_t cm_master_get_chest_fan_state(void) {
    if (g_master_mutex == NULL) {
        return 0;
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    uint8_t state = g_chest_fan_state;
    xSemaphoreGive(g_master_mutex);

    return state;
}

esp_err_t cm_master_get_incline_position(void) {
    cm_frame_t frame = {
        .len = 0,
        .seq = g_seq++,
        .cmd = CM_CMD_GET_INCLINE_POSITION,
    };

    ESP_LOGI(TAG, "Sending GET_INCLINE_POSITION");

    // GET_INCLINE_POSITION espera RSP_INCLINE_POSITION
    return send_frame_async(&frame, false); // false porque espera RSP, no ACK
}

float cm_master_get_current_incline(void) {
    if (g_master_mutex == NULL) {
        return 0.0f;
    }

    xSemaphoreTake(g_master_mutex, portMAX_DELAY);
    float incline = g_current_incline_pct;
    xSemaphoreGive(g_master_mutex);

    return incline;
}

esp_err_t cm_master_set_relay(uint8_t relay_id, uint8_t state) {
    // Validar parámetros
    if (relay_id == 0x01) { // WAX_PUMP
        if (state != 0x01) { // Solo se permite el estado ON para la bomba de cera (temporizada por esclavo)
            ESP_LOGE(TAG, "Invalid RELAY state for WAX_PUMP: only ON (0x01) is allowed. State received: %d", state);
            return ESP_ERR_INVALID_ARG;
        }
    } else { // Otros relés (si se implementan en el futuro)
        // Por ahora, solo la bomba de cera está definida.
        // Si se añaden otros relés, su validación iría aquí.
        ESP_LOGE(TAG, "Invalid RELAY ID: 0x%02X. Only WAX_PUMP (0x01) is currently supported.", relay_id);
        return ESP_ERR_INVALID_ARG;
    }

    cm_frame_t frame = {
        .len = 2,
        .seq = g_seq++,
        .cmd = CM_CMD_SET_RELAY,
    };
    frame.payload[0] = relay_id;
    frame.payload[1] = state;

    const char *relay_name = "WAX_PUMP"; // Solo WAX_PUMP es soportado actualmente
    ESP_LOGI(TAG, "Sending SET_RELAY: %s = ON (timed by slave)", relay_name);

    // SET_RELAY espera ACK
    return send_frame_async(&frame, true);
}
