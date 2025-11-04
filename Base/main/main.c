/**
 * @file main.c
 * @brief Sala de Maquinas - Esclavo (ESP32)
 */

#include "vfd_driver.h"
#include "speed_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <string.h>
#include <math.h>

// ===========================================================================
// CORRECCIÓN: DEFINICIONES DE PROTOCOLO FALTANTES
// ===========================================================================
#include "cm_protocol.h"
#include "cm_frame.h"   // <--- CORRECCIÓN: Include faltante para cm_frame_t

// Definición de estado de inclinación
typedef enum {
    INCLINE_MOTOR_STOPPED,
    INCLINE_MOTOR_UP,
    INCLINE_MOTOR_DOWN,
    INCLINE_MOTOR_HOMING
} incline_motor_state_t;

// Códigos NAK (Error)
#define NAK_INVALID_PAYLOAD CM_ERR_INVALID_PAYLOAD
#define NAK_UNKNOWN_CMD     CM_ERR_UNKNOWN_CMD
#define NAK_NOT_READY       CM_ERR_NOT_READY
#define NAK_VFD_FAULT       CM_ERR_VFD_FAULT
// ===========================================================================


static const char *TAG = "SLAVE";

// Factor de calibración (¡PLACEHOLDER!)
static float g_calibration_factor = 0.00875; // (10.5 km/h / 1200 pulsos/seg)
#define SPEED_UPDATE_INTERVAL_MS 500

// ===========================================================================
// CONFIGURACIÓN UART (a Consola v2.1)
// ===========================================================================
#define UART_PORT_NUM       UART_NUM_1
#define UART_BAUD_RATE      115200
#define UART_TX_PIN         17  // Asignación v5
#define UART_RX_PIN         16  // Asignación v5
#define UART_BUF_SIZE 512

// ===========================================================================
// ASIGNACIÓN DE PINES (v5)
// ===========================================================================
#define SENSOR_SPEED_PIN        34 // (PCNT)
#define INCLINE_LIMIT_SWITCH_PIN 35 // (Entrada)
#define HEAD_FAN_ON_OFF_PIN     26 // Relé 6
#define HEAD_FAN_SPEED_PIN      25 // Relé 7
#define CHEST_FAN_ON_OFF_PIN    33 // Relé 4
#define CHEST_FAN_SPEED_PIN     32 // Relé 5
#define INCLINE_UP_RELAY_PIN    27 // Relé 1
#define INCLINE_DOWN_RELAY_PIN  14 // Relé 2
#define WAX_PUMP_RELAY_PIN      13 // Relé 3

// ===========================================================================
// GLOBALES DE ESTADO
// ===========================================================================
SemaphoreHandle_t g_speed_mutex;
static bool g_emergency_state = false;
static uint64_t g_last_command_time_us = 0;
#define WATCHDOG_TIMEOUT_US (700 * 1000) // 700ms
static float g_real_speed_kmh = 0.0f;
static float g_target_speed_kmh = 0.0f;
static float g_real_incline_pct = 0.0f;
static float g_target_incline_pct = 0.0f;
static bool g_incline_is_calibrated = false;
static incline_motor_state_t g_incline_motor_state = INCLINE_MOTOR_STOPPED;
#define INCLINE_SPEED_PCT_PER_MS (0.05f / 1000.0f)  // Velocidad reducida a la mitad (doble tiempo)
static uint8_t g_head_fan_state = 0;
static uint8_t g_chest_fan_state = 0;
static uint8_t g_wax_pump_relay_state = 0;
static esp_timer_handle_t wax_pump_timer_handle;
#define WAX_PUMP_ACTIVATION_DURATION_MS 5000

// ===========================================================================
// TAREAS Y FUNCIONES DE BAJO NIVEL
// ===========================================================================
static void stop_incline_motor(void) {
    gpio_set_level(INCLINE_UP_RELAY_PIN, 0);
    gpio_set_level(INCLINE_DOWN_RELAY_PIN, 0);
    g_incline_motor_state = INCLINE_MOTOR_STOPPED;
}

static void enter_safe_state(void) {
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    if (!g_emergency_state) {
        ESP_LOGW(TAG, "⚠️ ENTERING SAFE STATE - Communication lost or emergency stop");
        g_emergency_state = true;
    }
    vfd_driver_emergency_stop(); // <-- CORRECCIÓN DE SEGURIDAD
    g_target_speed_kmh = 0.0f;
    g_target_incline_pct = 0.0f;
    g_head_fan_state = 0;
    g_chest_fan_state = 0;
    g_wax_pump_relay_state = 0;
    gpio_set_level(HEAD_FAN_ON_OFF_PIN, 0);
    gpio_set_level(HEAD_FAN_SPEED_PIN, 0);
    gpio_set_level(CHEST_FAN_ON_OFF_PIN, 0);
    gpio_set_level(CHEST_FAN_SPEED_PIN, 0);
    gpio_set_level(WAX_PUMP_RELAY_PIN, 0);
    stop_incline_motor();
    if (esp_timer_is_active(wax_pump_timer_handle)) {
        ESP_ERROR_CHECK(esp_timer_stop(wax_pump_timer_handle));
    }
    xSemaphoreGive(g_speed_mutex);
}

static void reset_safe_state(void) {
    if (g_emergency_state) {
        ESP_LOGI(TAG, "✅ SAFE STATE reset. Communication restored.");
        g_emergency_state = false;
    }
    g_last_command_time_us = esp_timer_get_time();
}

static void wax_pump_timer_callback(void *arg) {
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "Temporizador de bomba de cera finalizado, apagando relé.");
    gpio_set_level(WAX_PUMP_RELAY_PIN, 0);
    g_wax_pump_relay_state = 0;
    xSemaphoreGive(g_speed_mutex);
}

static void configure_gpios(void) {
    uint64_t output_pin_mask = (1ULL << HEAD_FAN_ON_OFF_PIN) | (1ULL << HEAD_FAN_SPEED_PIN) |
                               (1ULL << CHEST_FAN_ON_OFF_PIN) | (1ULL << CHEST_FAN_SPEED_PIN) |
                               (1ULL << INCLINE_UP_RELAY_PIN) | (1ULL << INCLINE_DOWN_RELAY_PIN) |
                               (1ULL << WAX_PUMP_RELAY_PIN);
    gpio_config_t io_conf_output = {
        .pin_bit_mask = output_pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf_output));

    // Configurar GPIO 35 con pull-up para anular sensor desconectado
    gpio_config_t io_conf_input = {
        .pin_bit_mask = (1ULL << INCLINE_LIMIT_SWITCH_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Pull-up para leer 1 cuando no está conectado
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf_input));
    ESP_LOGI(TAG, "GPIO %d configurado con pull-up (fin de carrera anulado)", INCLINE_LIMIT_SWITCH_PIN);

    gpio_set_level(HEAD_FAN_ON_OFF_PIN, 0);
    gpio_set_level(HEAD_FAN_SPEED_PIN, 0);
    gpio_set_level(CHEST_FAN_ON_OFF_PIN, 0);
    gpio_set_level(CHEST_FAN_SPEED_PIN, 0);
    gpio_set_level(INCLINE_UP_RELAY_PIN, 0);
    gpio_set_level(INCLINE_DOWN_RELAY_PIN, 0);
    gpio_set_level(WAX_PUMP_RELAY_PIN, 0);
    ESP_LOGI(TAG, "GPIOs configurados. Asignación v5 (Sensores en 34, 35).");
}

// ===========================================================================
// LÓGICA DE PROTOCOLO v2.1 (RESPUESTAS)
// ===========================================================================

/**
 * @brief Envía una trama completa por UART
 */
static esp_err_t send_frame(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint8_t len) {
    cm_frame_t frame = {
        .cmd = cmd,
        .seq = seq,
        .len = len
    };

    if (len > 0 && payload != NULL) {
        memcpy(frame.payload, payload, len);
    }

    uint8_t buffer[CM_MAX_STUFFED_SIZE];
    size_t frame_len = cm_build_frame(&frame, buffer, sizeof(buffer));

    if (frame_len == 0) {
        ESP_LOGE(TAG, "Error al construir trama");
        return ESP_FAIL;
    }

    int written = uart_write_bytes(UART_PORT_NUM, buffer, frame_len);
    if (written < 0) {
        ESP_LOGE(TAG, "Error al enviar trama por UART");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Trama enviada: CMD=0x%02X, SEQ=%d, LEN=%d", cmd, seq, len);
    return ESP_OK;
}

/**
 * @brief Envía ACK
 */
static esp_err_t send_ack(uint8_t seq) {
    return send_frame(CM_RSP_ACK, seq, &seq, 1);
}

/**
 * @brief Envía NAK con código de error
 */
static esp_err_t send_nak(uint8_t seq, uint8_t error_code) {
    uint8_t payload[2] = { seq, error_code };
    return send_frame(CM_RSP_NAK, seq, payload, 2);
}

static void send_status(uint8_t seq) {
    uint8_t status_payload = 0x00;

    // Bit 0: Estado del VFD (1 = Fallo o Desconectado, 0 = OK)
    vfd_status_t vfd_status = vfd_driver_get_status();
    if (vfd_status != VFD_STATUS_OK) {
        status_payload |= 0x01;
    }

    send_frame(CM_RSP_STATUS, seq, &status_payload, 1);
}

static void send_sensor_speed(uint8_t seq) {
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    float real_speed = g_real_speed_kmh;
    xSemaphoreGive(g_speed_mutex);
    uint16_t speed_x100 = (uint16_t)(real_speed * 100.0f);
    uint8_t payload[2] = { (uint8_t)((speed_x100 >> 8) & 0xFF), (uint8_t)(speed_x100 & 0xFF) };
    send_frame(CM_RSP_SENSOR_SPEED, seq, payload, 2); // <-- CORREGIDO
}

static void send_incline_position(uint8_t seq) {
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    float real_incline = g_real_incline_pct;
    xSemaphoreGive(g_speed_mutex);
    uint16_t incline_x10 = (uint16_t)(real_incline * 10.0f);
    uint8_t payload[2] = { (uint8_t)((incline_x10 >> 8) & 0xFF), (uint8_t)(incline_x10 & 0xFF) };
    send_frame(CM_RSP_INCLINE_POSITION, seq, payload, 2); // <-- CORREGIDO
}

static void send_fan_state(uint8_t seq) {
    uint8_t payload[2];

    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    payload[0] = g_head_fan_state;
    payload[1] = g_chest_fan_state;
    xSemaphoreGive(g_speed_mutex);

    send_frame(CM_RSP_FAN_STATE, seq, payload, 2);
}

// ===========================================================================
// LÓGICA DE PROTOCOLO v2.1 (PROCESADORES DE COMANDOS)
// ===========================================================================

static void process_set_speed(uint8_t seq, const uint8_t *payload, uint8_t len) {
    if (len != 2) { send_nak(seq, NAK_INVALID_PAYLOAD); return; }

    // Verificar estado del VFD antes de aceptar el comando
    vfd_status_t vfd_status = vfd_driver_get_status();
    if (vfd_status == VFD_STATUS_FAULT) {
        ESP_LOGE(TAG, "Rechazando SET_SPEED: VFD en estado de FALLO");
        send_nak(seq, NAK_VFD_FAULT);
        return;
    }
    if (vfd_status == VFD_STATUS_DISCONNECTED) {
        ESP_LOGE(TAG, "Rechazando SET_SPEED: VFD desconectado");
        send_nak(seq, NAK_VFD_FAULT);
        return;
    }

    uint16_t target_speed_raw = (payload[0] << 8) | payload[1];
    float target_speed = (float)target_speed_raw / 100.0f;
    if (target_speed < 0) target_speed = 0;
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_target_speed_kmh = target_speed;
    xSemaphoreGive(g_speed_mutex);
    vfd_driver_set_speed(target_speed);
    send_ack(seq);
}

static void process_set_incline(uint8_t seq, const uint8_t *payload, uint8_t len) {
    if (len != 2) { send_nak(seq, NAK_INVALID_PAYLOAD); return; }
    uint16_t target_incline_raw = (payload[0] << 8) | payload[1];
    float target_incline = (float)target_incline_raw / 10.0f;
    if (target_incline < 0) target_incline = 0;
    if (!g_incline_is_calibrated) { send_nak(seq, NAK_NOT_READY); return; }
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_target_incline_pct = target_incline;
    xSemaphoreGive(g_speed_mutex);
    send_ack(seq);
}

static void process_calibrate_incline(uint8_t seq) {
    ESP_LOGI(TAG, "CALIBRATE_INCLINE: Iniciando rutina de Homing");
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_incline_motor_state = INCLINE_MOTOR_HOMING;
    g_incline_is_calibrated = false;
    xSemaphoreGive(g_speed_mutex);
    send_ack(seq);
}

static void process_set_fan_state(uint8_t seq, const uint8_t *payload, uint8_t len) {
    if (len != 2) { send_nak(seq, NAK_INVALID_PAYLOAD); return; }
    uint8_t fan_id = payload[0];
    uint8_t fan_state = payload[1];
    if (fan_state > 2) { send_nak(seq, NAK_INVALID_PAYLOAD); return; }

    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    if (fan_id == 0x01) { // Ventilador Cabeza
        g_head_fan_state = fan_state;
        gpio_set_level(HEAD_FAN_ON_OFF_PIN, (fan_state > 0) ? 1 : 0);
        gpio_set_level(HEAD_FAN_SPEED_PIN, (fan_state == 2) ? 1 : 0);
    } else if (fan_id == 0x02) { // Ventilador Pecho
        g_chest_fan_state = fan_state;
        gpio_set_level(CHEST_FAN_ON_OFF_PIN, (fan_state > 0) ? 1 : 0);
        gpio_set_level(CHEST_FAN_SPEED_PIN, (fan_state == 2) ? 1 : 0);
    } else {
        xSemaphoreGive(g_speed_mutex);
        send_nak(seq, NAK_INVALID_PAYLOAD);
        return;
    }
    xSemaphoreGive(g_speed_mutex);
    send_ack(seq);
}

static void process_set_relay(uint8_t seq, const uint8_t *payload, uint8_t len) {
    if (len != 2) { send_nak(seq, NAK_INVALID_PAYLOAD); return; }
    uint8_t relay_id = payload[0];
    uint8_t relay_state = payload[1];
    if (relay_id == 0x01) { // Bomba de Cera
        if (relay_state == 1) {
            ESP_LOGI(TAG, "SET_RELAY: Activando bomba de cera por 5 segundos");
            xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
            gpio_set_level(WAX_PUMP_RELAY_PIN, 1);
            g_wax_pump_relay_state = 1;
            xSemaphoreGive(g_speed_mutex);
            ESP_ERROR_CHECK(esp_timer_start_once(wax_pump_timer_handle, WAX_PUMP_ACTIVATION_DURATION_MS * 1000));
        } else {
            xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
            gpio_set_level(WAX_PUMP_RELAY_PIN, 0);
            g_wax_pump_relay_state = 0;
            xSemaphoreGive(g_speed_mutex);
            if (esp_timer_is_active(wax_pump_timer_handle)) {
                ESP_ERROR_CHECK(esp_timer_stop(wax_pump_timer_handle));
            }
        }
    } else {
        send_nak(seq, NAK_INVALID_PAYLOAD);
        return;
    }
    send_ack(seq);
}

static void process_emergency_stop(uint8_t seq) {
    ESP_LOGW(TAG, "🚨 EMERGENCY_STOP received!");
    enter_safe_state();
    send_ack(seq);
}

/**
 * @brief Procesa una trama v2.1 válida
 */
static void process_frame(const cm_frame_t *frame) {
    reset_safe_state();
    switch (frame->cmd) {
        case CM_CMD_GET_STATUS: send_status(frame->seq); break;
        case CM_CMD_GET_SENSOR_SPEED: send_sensor_speed(frame->seq); break;
        case CM_CMD_GET_INCLINE_POSITION: send_incline_position(frame->seq); break;
        case CM_CMD_GET_FAN_STATE: send_fan_state(frame->seq); break;
        case CM_CMD_SET_SPEED: process_set_speed(frame->seq, frame->payload, frame->len); break;
        case CM_CMD_SET_INCLINE: process_set_incline(frame->seq, frame->payload, frame->len); break;
        case CM_CMD_CALIBRATE_INCLINE: process_calibrate_incline(frame->seq); break;
        case CM_CMD_SET_FAN_STATE: process_set_fan_state(frame->seq, frame->payload, frame->len); break;
        case CM_CMD_SET_RELAY: process_set_relay(frame->seq, frame->payload, frame->len); break;
        case CM_CMD_EMERGENCY_STOP: process_emergency_stop(frame->seq); break;
        default:
            ESP_LOGW(TAG, "Comando desconocido: 0x%02X", frame->cmd);
            send_nak(frame->seq, NAK_UNKNOWN_CMD);
            break;
    }
}

// ===========================================================================
// PARSER SIMPLE BYTE-A-BYTE
// ===========================================================================

typedef enum {
    PARSER_WAIT_SOF,
    PARSER_READ_FRAME
} parser_state_enum_t;

typedef struct {
    parser_state_enum_t state;
    uint8_t buffer[CM_MAX_STUFFED_SIZE];
    size_t buffer_len;
    int64_t last_byte_time;
} cm_parser_state_t;

typedef enum {
    CM_PARSE_SUCCESS,
    CM_PARSE_INCOMPLETE,
    CM_PARSE_ERROR
} cm_parse_result_t;

static void cm_parser_init(cm_parser_state_t *parser) {
    parser->state = PARSER_WAIT_SOF;
    parser->buffer_len = 0;
    parser->last_byte_time = 0;
}

static bool cm_parser_timeout(cm_parser_state_t *parser, uint32_t timeout_ms) {
    if (parser->buffer_len > 0 && parser->last_byte_time > 0) {
        int64_t elapsed = (esp_timer_get_time() - parser->last_byte_time) / 1000;
        if (elapsed > timeout_ms) {
            parser->state = PARSER_WAIT_SOF;
            parser->buffer_len = 0;
            return true;
        }
    }
    return false;
}

static cm_parse_result_t cm_parse_byte(cm_parser_state_t *parser, uint8_t byte, cm_frame_t *frame) {
    parser->last_byte_time = esp_timer_get_time();

    if (parser->state == PARSER_WAIT_SOF) {
        if (byte == CM_SOF) {
            parser->buffer[0] = byte;
            parser->buffer_len = 1;
            parser->state = PARSER_READ_FRAME;
        }
        return CM_PARSE_INCOMPLETE;
    }

    // PARSER_READ_FRAME
    if (parser->buffer_len >= CM_MAX_STUFFED_SIZE) {
        parser->state = PARSER_WAIT_SOF;
        parser->buffer_len = 0;
        return CM_PARSE_ERROR;
    }

    parser->buffer[parser->buffer_len++] = byte;

    // Intentar parsear cuando tengamos al menos el tamaño mínimo
    if (parser->buffer_len >= CM_MIN_FRAME_SIZE + 1) {  // +1 por el SOF
        if (cm_parse_frame(parser->buffer, parser->buffer_len, frame)) {
            parser->state = PARSER_WAIT_SOF;
            parser->buffer_len = 0;
            return CM_PARSE_SUCCESS;
        }
    }

    return CM_PARSE_INCOMPLETE;
}

// ===========================================================================
// TAREAS RTOS
// ===========================================================================

static void uart_rx_task(void *pvParameters) {
    uint8_t data[UART_BUF_SIZE];
    cm_parser_state_t parser_state;
    cm_frame_t frame;
    cm_parser_init(&parser_state);
    ESP_LOGI(TAG, "Tarea UART RX iniciada. Escuchando en UART%d...", UART_PORT_NUM);
    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, data, UART_BUF_SIZE, 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                if (cm_parse_byte(&parser_state, data[i], &frame) == CM_PARSE_SUCCESS) {
                    ESP_LOGD(TAG, "Trama recibida: CMD=0x%02X, SEQ=%d, LEN=%d", frame.cmd, frame.seq, frame.len);
                    process_frame(&frame);
                }
            }
        }
        if (cm_parser_timeout(&parser_state, 100)) {
             ESP_LOGW(TAG, "Parser timeout, reset a SOF");
        }
    }
}

static void speed_update_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tarea de actualización de velocidad iniciada");
    int64_t last_read_time_us = esp_timer_get_time();
    
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SPEED_UPDATE_INTERVAL_MS)); // <-- CORREGIDO (Errata)
        
        int64_t now_us = esp_timer_get_time();
        int64_t delta_time_us = now_us - last_read_time_us;
        last_read_time_us = now_us;

        int pulse_count = speed_sensor_get_pulse_count();
        float pulses_per_sec = (float)pulse_count * 1000000.0 / (float)delta_time_us;
        float new_real_speed = pulses_per_sec * g_calibration_factor;
        
        xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
        g_real_speed_kmh = new_real_speed;
        xSemaphoreGive(g_speed_mutex);
    }
}

static void incline_control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tarea de control de inclinación iniciada");
    g_incline_is_calibrated = false;
    g_incline_motor_state = INCLINE_MOTOR_HOMING;
    uint64_t last_update_time_us = esp_timer_get_time();

    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(50)); // <-- CORREGIDO (Errata)
        uint64_t now_us = esp_timer_get_time();
        uint64_t delta_us = now_us - last_update_time_us;
        last_update_time_us = now_us;
        float delta_ms = (float)delta_us / 1000.0f;

        xSemaphoreTake(g_speed_mutex, portMAX_DELAY);

        if (g_emergency_state) {
            xSemaphoreGive(g_speed_mutex);
            continue;
        }

        switch (g_incline_motor_state) {
            case INCLINE_MOTOR_STOPPED:
                if (!g_incline_is_calibrated) {
                    g_incline_motor_state = INCLINE_MOTOR_HOMING;
                } else {
                    float error = g_target_incline_pct - g_real_incline_pct;
                    if (fabs(error) > 0.1) {
                        if (error > 0) {
                            g_incline_motor_state = INCLINE_MOTOR_UP;
                            gpio_set_level(INCLINE_UP_RELAY_PIN, 1);
                        } else {
                            g_incline_motor_state = INCLINE_MOTOR_DOWN;
                            gpio_set_level(INCLINE_DOWN_RELAY_PIN, 1);
                        }
                    }
                }
                break;
            case INCLINE_MOTOR_HOMING:
                // TEMPORAL: Sensor de fin de carrera desconectado - anular homing
                // if (gpio_get_level(INCLINE_LIMIT_SWITCH_PIN) == 0) {
                //     ESP_LOGI(TAG, "Homing de inclinación completado.");
                //     stop_incline_motor();
                //     g_real_incline_pct = 0.0f;
                //     g_target_incline_pct = 0.0f;
                //     g_incline_is_calibrated = true;
                // } else {
                //     gpio_set_level(INCLINE_UP_RELAY_PIN, 0);
                //     gpio_set_level(INCLINE_DOWN_RELAY_PIN, 1);
                // }
                // Completar homing inmediatamente sin sensor
                ESP_LOGI(TAG, "Homing de inclinación completado (sin sensor).");
                stop_incline_motor();
                g_real_incline_pct = 0.0f;
                g_target_incline_pct = 0.0f;
                g_incline_is_calibrated = true;
                break;
            case INCLINE_MOTOR_UP:
                g_real_incline_pct += (delta_ms * INCLINE_SPEED_PCT_PER_MS);
                if (g_real_incline_pct >= g_target_incline_pct) {
                    stop_incline_motor();
                    g_real_incline_pct = g_target_incline_pct;
                }
                break;
            case INCLINE_MOTOR_DOWN:
                g_real_incline_pct -= (delta_ms * INCLINE_SPEED_PCT_PER_MS);
                // TEMPORAL: Sensor de fin de carrera desconectado - solo usar objetivo
                // if (gpio_get_level(INCLINE_LIMIT_SWITCH_PIN) == 0) {
                //     stop_incline_motor();
                //     g_real_incline_pct = 0.0f;
                //     g_incline_is_calibrated = true;
                // } else
                if (g_real_incline_pct <= g_target_incline_pct) {
                    stop_incline_motor();
                    g_real_incline_pct = g_target_incline_pct;
                }
                break;
        }
        xSemaphoreGive(g_speed_mutex);
    }
}

static void watchdog_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tarea de watchdog iniciada (Timeout: %llu ms)", WATCHDOG_TIMEOUT_US / 1000);
    vTaskDelay(pdMS_TO_TICKS(2000)); // <-- CORREGIDO (Errata)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100)); // <-- CORREGIDO (Errata)
        if (g_emergency_state) {
            continue;
        }
        uint64_t time_since_last_cmd = esp_timer_get_time() - g_last_command_time_us;
        if (time_since_last_cmd > WATCHDOG_TIMEOUT_US) {
            ESP_LOGE(TAG, "¡WATCHDOG TIMEOUT! No se recibió comando en %llu ms", WATCHDOG_TIMEOUT_US / 1000);
            enter_safe_state();
        }
    }
}

// ===========================================================================
// FUNCIÓN PRINCIPAL
// ===========================================================================
void app_main(void) {
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  Sala de Maquinas - Sistema Esclavo");
    ESP_LOGI(TAG, "  FASE 9: Integración de Hardware");
    ESP_LOGI(TAG, "==============================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_speed_mutex = xSemaphoreCreateMutex();
    if (g_speed_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    configure_gpios();
    
    const esp_timer_create_args_t wax_pump_timer_args = {
            .callback = &wax_pump_timer_callback,
            .name = "wax_pump_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&wax_pump_timer_args, &wax_pump_timer_handle));
    ESP_LOGI(TAG, "Temporizador de bomba de cera creado.");

    speed_sensor_init();

    ESP_LOGI(TAG, "Configurando UART para RS485...");
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM,
                                  UART_TX_PIN,
                                  UART_RX_PIN,
                                  UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART%d configurado: %d baud, TX=%d, RX=%d",
             UART_PORT_NUM, UART_BAUD_RATE, UART_TX_PIN, UART_RX_PIN);

    // ========================================================================
    // CREAR TAREAS
    // ========================================================================

    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "Tarea UART RX creada");

    vfd_driver_init();
    ESP_LOGI(TAG, "Controlador VFD (real) inicializado");

    xTaskCreate(speed_update_task, "speed_update_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Tarea de actualización de velocidad creada");

    xTaskCreate(incline_control_task, "incline_ctrl_task", 4096, NULL, 7, NULL); 
    ESP_LOGI(TAG, "Tarea de control de inclinación creada");

    xTaskCreate(watchdog_task, "watchdog_task", 2048, NULL, 6, NULL);
    ESP_LOGI(TAG, "Tarea de watchdog creada (timeout: 700ms)");

    ESP_LOGI(TAG, "Sistema iniciado correctamente");
    ESP_LOGI(TAG, "Esperando comandos del Maestro...");

    // Heartbeat loop (para depuración)
    uint32_t heartbeat_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10 segundos // <-- CORREGIDO (Errata)
        
        xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
        float r_speed = g_real_speed_kmh;
        float t_speed = g_target_speed_kmh;
        float r_incline = g_real_incline_pct;
        float t_incline = g_target_incline_pct;
        xSemaphoreGive(g_speed_mutex);

        ESP_LOGI(TAG, "Heartbeat #%lu - Speed: %.2f/%.2f km/h, Incline: %.1f/%.1f %%",
                 heartbeat_count++, r_speed, t_speed,
                 r_incline, t_incline);
    }
}