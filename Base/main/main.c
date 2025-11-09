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
// PROTOCOLO ASCII SIMPLE
// ===========================================================================

// Definición de estado de inclinación
typedef enum {
    INCLINE_MOTOR_STOPPED,
    INCLINE_MOTOR_UP,
    INCLINE_MOTOR_DOWN,
    INCLINE_MOTOR_HOMING
} incline_motor_state_t;

// Códigos de error para ACK
#define ACK_OK              "OK"
#define ACK_ERROR_VFD_FAULT "VFD_FAULT"
#define ACK_ERROR_OUT_OF_RANGE "OUT_OF_RANGE"
#define ACK_ERROR_NOT_CALIBRATED "NOT_CALIBRATED"
#define ACK_ERROR_UNKNOWN_CMD "UNKNOWN_CMD"
#define ACK_ERROR_RELAY_FAULT "RELAY_FAULT"

// Buffer para líneas UART
#define UART_LINE_BUFFER_SIZE 128
// ===========================================================================


static const char *TAG = "SLAVE";

// Factor de calibración CALIBRADO con hardware real
// Datos de calibración: 78.10 Hz → 10.00 km/h → 575 pulsos/seg promedio
// Cálculo: g_calibration_factor = 10.00 km/h / 575 pulsos/seg = 0.0174
static float g_calibration_factor = 0.0174; // Calibrado 2025-11-06 con corona de 12 dientes
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
#define SENSOR_SPEED_PIN        15 // (PCNT) - Sensor Hall con corona de 12 dientes
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
#define WATCHDOG_TIMEOUT_US (1000 * 1000) // 1000ms (1 segundo)
static float g_real_speed_kmh = 0.0f;
static float g_target_speed_kmh = 0.0f;
static float g_real_incline_pct = 0.0f;
static float g_target_incline_pct = 0.0f;
static bool g_incline_is_calibrated = false;
static incline_motor_state_t g_incline_motor_state = INCLINE_MOTOR_STOPPED;
#define INCLINE_SPEED_PCT_PER_MS (1.5f / 1000.0f)  // 0-15% en 10 segundos (1.5%/segundo)
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
// PROTOCOLO ASCII SIMPLE (RESPUESTAS)
// ===========================================================================

/**
 * @brief Envía una línea de texto por UART
 */
static esp_err_t send_line(const char *line) {
    int len = strlen(line);
    int written = uart_write_bytes(UART_PORT_NUM, line, len);
    if (written < 0) {
        ESP_LOGE(TAG, "Error al enviar línea por UART");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Línea enviada: %s", line);
    return ESP_OK;
}

/**
 * @brief Envía ACK=OK
 */
static void send_ack_ok(void) {
    send_line("ACK=OK\n");
}

/**
 * @brief Envía ACK=ERROR,<motivo>
 */
static void send_ack_error(const char *error_code) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "ACK=ERROR,%s\n", error_code);
    send_line(buffer);
}

/**
 * @brief Envía respuesta DATA consolidada
 * Formato: DATA=<speed>,<incline>,<vfd_freq>,<vfd_fault>,<fan_head>,<fan_chest>
 */
static void send_data_response(void) {
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    float real_speed = g_real_speed_kmh;
    float real_incline = g_real_incline_pct;
    uint8_t head_fan = g_head_fan_state;
    uint8_t chest_fan = g_chest_fan_state;
    xSemaphoreGive(g_speed_mutex);

    // Obtener frecuencia real del VFD
    float vfd_freq = vfd_driver_get_real_freq_hz();

    // Obtener código de fallo del VFD
    vfd_status_t vfd_status = vfd_driver_get_status();
    uint8_t vfd_fault = (vfd_status == VFD_STATUS_OK) ? 0 : 1;

    char buffer[96];
    snprintf(buffer, sizeof(buffer), "DATA=%.2f,%.1f,%.2f,%d,%d,%d\n",
             real_speed, real_incline, vfd_freq, vfd_fault, head_fan, chest_fan);
    send_line(buffer);
}

// ===========================================================================
// PROTOCOLO ASCII (PROCESADORES DE COMANDOS)
// ===========================================================================

/**
 * @brief Extrae el valor flotante de un comando "CMD=valor"
 */
static float extract_float_value(const char *cmd_line) {
    const char *equals = strchr(cmd_line, '=');
    if (equals == NULL) return 0.0f;
    return atof(equals + 1);
}

/**
 * @brief Extrae el valor entero de un comando "CMD=valor"
 */
static int extract_int_value(const char *cmd_line) {
    const char *equals = strchr(cmd_line, '=');
    if (equals == NULL) return 0;
    return atoi(equals + 1);
}

/**
 * @brief Procesa SET_SPEED=<kph>
 */
static void process_set_speed(float target_speed) {
    // Verificar estado del VFD
    vfd_status_t vfd_status = vfd_driver_get_status();
    if (vfd_status == VFD_STATUS_FAULT) {
        ESP_LOGE(TAG, "Rechazando SET_SPEED: VFD en FALLO");
        send_ack_error(ACK_ERROR_VFD_FAULT);
        return;
    }
    if (vfd_status == VFD_STATUS_DISCONNECTED) {
        ESP_LOGE(TAG, "Rechazando SET_SPEED: VFD desconectado");
        send_ack_error(ACK_ERROR_VFD_FAULT);
        return;
    }

    // Validar rango
    if (target_speed < 0 || target_speed > 20.0f) {
        ESP_LOGE(TAG, "Velocidad fuera de rango: %.2f km/h", target_speed);
        send_ack_error(ACK_ERROR_OUT_OF_RANGE);
        return;
    }

    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_target_speed_kmh = target_speed;
    xSemaphoreGive(g_speed_mutex);

    vfd_driver_set_speed(target_speed);
    send_ack_ok();
    ESP_LOGI(TAG, "SET_SPEED: %.2f km/h", target_speed);
}

/**
 * @brief Procesa SET_INCLINE=<pct>
 */
static void process_set_incline(float target_incline) {
    if (!g_incline_is_calibrated) {
        send_ack_error(ACK_ERROR_NOT_CALIBRATED);
        return;
    }

    // Validar rango
    if (target_incline < 0 || target_incline > 15.0f) {
        send_ack_error(ACK_ERROR_OUT_OF_RANGE);
        return;
    }

    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_target_incline_pct = target_incline;
    xSemaphoreGive(g_speed_mutex);

    send_ack_ok();
    ESP_LOGI(TAG, "SET_INCLINE: %.1f%%", target_incline);
}

/**
 * @brief Procesa CALIBRATE_INCLINE=1
 */
static void process_calibrate_incline(void) {
    ESP_LOGI(TAG, "CALIBRATE_INCLINE: Iniciando rutina de homing");
    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_incline_motor_state = INCLINE_MOTOR_HOMING;
    g_incline_is_calibrated = false;
    xSemaphoreGive(g_speed_mutex);
    send_ack_ok();
}

/**
 * @brief Procesa SET_FAN_HEAD=<state> (0=off, 1=low, 2=high)
 * No envía ACK, el P4 verificará el estado con GET_DATA
 */
static void process_set_fan_head(int fan_state) {
    if (fan_state < 0 || fan_state > 2) return;

    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_head_fan_state = fan_state;
    xSemaphoreGive(g_speed_mutex);

    gpio_set_level(HEAD_FAN_ON_OFF_PIN, (fan_state > 0) ? 1 : 0);
    gpio_set_level(HEAD_FAN_SPEED_PIN, (fan_state == 2) ? 1 : 0);

    ESP_LOGI(TAG, "SET_FAN_HEAD: %d", fan_state);
}

/**
 * @brief Procesa SET_FAN_CHEST=<state>
 * No envía ACK, el P4 verificará el estado con GET_DATA
 */
static void process_set_fan_chest(int fan_state) {
    if (fan_state < 0 || fan_state > 2) return;

    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    g_chest_fan_state = fan_state;
    xSemaphoreGive(g_speed_mutex);

    gpio_set_level(CHEST_FAN_ON_OFF_PIN, (fan_state > 0) ? 1 : 0);
    gpio_set_level(CHEST_FAN_SPEED_PIN, (fan_state == 2) ? 1 : 0);

    ESP_LOGI(TAG, "SET_FAN_CHEST: %d", fan_state);
}

/**
 * @brief Procesa CMD_WAX=1 (activar bomba de cera por 5 segundos)
 */
static void process_cmd_wax(void) {
    ESP_LOGI(TAG, "CMD_WAX: Activando bomba de cera por 5 segundos");

    xSemaphoreTake(g_speed_mutex, portMAX_DELAY);
    gpio_set_level(WAX_PUMP_RELAY_PIN, 1);
    g_wax_pump_relay_state = 1;
    xSemaphoreGive(g_speed_mutex);

    esp_err_t err = esp_timer_start_once(wax_pump_timer_handle, WAX_PUMP_ACTIVATION_DURATION_MS * 1000);
    if (err == ESP_OK) {
        send_ack_ok();
    } else {
        ESP_LOGE(TAG, "Error al iniciar timer de bomba de cera");
        send_ack_error(ACK_ERROR_RELAY_FAULT);
    }
}

/**
 * @brief Procesa CMD_STOP=1
 */
static void process_cmd_stop(void) {
    ESP_LOGW(TAG, "🚨 CMD_STOP received!");
    enter_safe_state();
    send_ack_ok();
}

/**
 * @brief Procesa GET_DATA=1
 * Envía respuesta DATA consolidada
 */
static void process_get_data(void) {
    send_data_response();
}

/**
 * @brief Procesa un comando ASCII recibido
 */
static void process_command(const char *cmd_line) {
    reset_safe_state();

    ESP_LOGD(TAG, "Comando recibido: %s", cmd_line);

    // Comandos con ACK
    if (strncmp(cmd_line, "SET_SPEED=", 10) == 0) {
        float speed = extract_float_value(cmd_line);
        process_set_speed(speed);
    }
    else if (strncmp(cmd_line, "SET_INCLINE=", 12) == 0) {
        float incline = extract_float_value(cmd_line);
        process_set_incline(incline);
    }
    else if (strncmp(cmd_line, "CALIBRATE_INCLINE=", 18) == 0) {
        process_calibrate_incline();
    }
    else if (strncmp(cmd_line, "CMD_WAX=", 8) == 0) {
        process_cmd_wax();
    }
    else if (strncmp(cmd_line, "CMD_STOP=", 9) == 0) {
        process_cmd_stop();
    }
    // Comandos sin ACK
    else if (strncmp(cmd_line, "SET_FAN_HEAD=", 13) == 0) {
        int state = extract_int_value(cmd_line);
        process_set_fan_head(state);
    }
    else if (strncmp(cmd_line, "SET_FAN_CHEST=", 14) == 0) {
        int state = extract_int_value(cmd_line);
        process_set_fan_chest(state);
    }
    // Comando de lectura
    else if (strncmp(cmd_line, "GET_DATA=", 9) == 0) {
        process_get_data();
    }
    else {
        ESP_LOGW(TAG, "Comando desconocido: %s", cmd_line);
        send_ack_error(ACK_ERROR_UNKNOWN_CMD);
    }
}

// ===========================================================================
// PARSER ASCII SIMPLE (LÍNEA A LÍNEA)
// ===========================================================================

/**
 * @brief Tarea de recepción UART - Lee líneas terminadas en \n
 */
static void uart_rx_task(void *pvParameters) {
    char line_buffer[UART_LINE_BUFFER_SIZE];
    size_t line_pos = 0;

    ESP_LOGI(TAG, "Tarea UART RX iniciada (modo ASCII). Escuchando en UART%d...", UART_PORT_NUM);

    while (1) {
        uint8_t byte;
        int len = uart_read_bytes(UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            // Detectar fin de línea
            if (byte == '\n' || byte == '\r') {
                if (line_pos > 0) {
                    // Terminar string y procesar comando
                    line_buffer[line_pos] = '\0';
                    process_command(line_buffer);
                    line_pos = 0;
                }
            }
            // Acumular caracteres (ignorar \r adicionales)
            else if (byte >= 32 && byte < 127) {  // Solo caracteres imprimibles ASCII
                if (line_pos < UART_LINE_BUFFER_SIZE - 1) {
                    line_buffer[line_pos++] = byte;
                } else {
                    // Buffer lleno, descartar línea
                    ESP_LOGW(TAG, "Línea demasiado larga, descartando");
                    line_pos = 0;
                }
            }
        }
    }
}

// ===========================================================================
// TAREAS RTOS
// ===========================================================================

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
    ESP_LOGI(TAG, "  PROTOCOLO: ASCII Simple (UART Direct)");
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
    ESP_LOGI(TAG, "Tarea de watchdog creada (timeout: 1000ms)");

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