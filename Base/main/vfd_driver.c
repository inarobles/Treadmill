/**
 * @file vfd_driver.c
 * @brief Driver para control del VFD SU300 vía Modbus RTU
 *
 * Este driver implementa el control completo del variador de frecuencia
 * mediante comunicación Modbus RTU sobre UART2.
 */

// --- Includes ---
#include "vfd_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/uart.h"

// Includes de ESP-MODBUS
#include "esp_modbus_master.h"
#include "esp_modbus_common.h"
#include "driver/gpio.h"

// Códigos de función Modbus estándar
#define MB_FUNC_WRITE_SINGLE_REGISTER  0x06
#define MB_FUNC_READ_HOLDING_REGISTERS 0x03

// --- Definiciones de Hardware (Pines) ---
#define VFD_UART_PORT      (UART_NUM_2)
#define VFD_TX_PIN         (19)
#define VFD_RX_PIN         (18)
#define VFD_RTS_PIN        (UART_PIN_NO_CHANGE) // -1, crucial para hardware con auto-dirección

// --- Definiciones de Protocolo (Basado en Manual SU300) ---
#define VFD_SLAVE_ID       1   // ID Modbus del VFD (asumido 1)
#define VFD_BAUD_RATE      9600 // (Debe coincidir con parámetro F5-00 del VFD)
#define VFD_PARITY         UART_PARITY_DISABLE // (Debe coincidir con F5-02 del VFD)

// Registros de control SU300 (Confirmados en el manual)
#define VFD_REG_CONTROL    0x2000 // (Comando Run/Stop)
#define VFD_REG_FREQ       0x2001 // (Consigna de Frecuencia)

// REGISTROS DE LECTURA (Monitorización)
#define VFD_REG_REAL_FREQ  0x2103 // (Frecuencia real aplicada por el VFD, Hz × 100)
#define VFD_REG_FAULT_CODE 0x2104 // (Lectura de código de fallo, 0 = Sin fallo)

// Comandos para 0x2000
#define VFD_CMD_RUN_FWD    0x0001
#define VFD_CMD_STOP       0x0005 // (F-STOP)

// Registros de parámetros (para configuración inicial)
#define VFD_REG_F0_01      0x0001 // (Fuente de Comando)
#define VFD_REG_F0_02      0x0002 // (Fuente de Frecuencia)

// --- Constantes del Driver ---
// CALIBRADO con hardware real: 10.00 km/h = 78.10 Hz
// Fórmula del VFD SU300: km/h = Hz × (6.4 / 50.0) → Hz = km/h × (50.0 / 6.4)
// Por tanto: 20 km/h = 156.25 Hz (NO 60 Hz como se asumía antes)
// Ratio: Hz/km/h = 50.0/6.4 = 7.8125
#define KPH_TO_HZ_RATIO    (50.0f / 6.4f) // Calibrado 2025-11-06: 7.8125 Hz/km/h
#define VFD_TASK_STACK     4096
#define VFD_TASK_PRIO      8
#define VFD_POLL_MS        200 // (Frecuencia de actualización de velocidad)

// ===========================================================================
// VARIABLES GLOBALES (ESTÁTICAS)
// ===========================================================================

static const char *TAG_VFD = "VFD_DRIVER_MODBUS";

// Handle del maestro Modbus
static void *master_handle = NULL;

// Variables de estado protegidas por mutex
static SemaphoreHandle_t vfd_mutex = NULL;
static vfd_status_t g_vfd_status = VFD_STATUS_DISCONNECTED;
static float g_target_kph = 0.0;
static float g_current_freq_hz = 0.0;
static float g_vfd_real_freq_hz = 0.0;  // Frecuencia real leída del VFD (0x2103)
static bool g_emergency_stop = false;

// Tarea de control
static TaskHandle_t vfd_task_handle = NULL;

// ===========================================================================
// PROTOTIPOS DE FUNCIONES PRIVADAS
// ===========================================================================

static esp_err_t vfd_modbus_init(void);
static void vfd_control_task(void *pvParameters);
static esp_err_t vfd_write_register(uint16_t reg_addr, uint16_t value);
static esp_err_t vfd_read_register(uint16_t reg_addr, uint16_t *value);
static esp_err_t vfd_check_and_configure_params(void);

// ===========================================================================
// IMPLEMENTACIÓN DE LA API PÚBLICA (vfd_driver.h)
// ===========================================================================

void vfd_driver_init(void) {
    vfd_mutex = xSemaphoreCreateMutex();
    if (vfd_mutex == NULL) {
        ESP_LOGE(TAG_VFD, "Error creando vfd_mutex");
        return;
    }

    if (vfd_modbus_init() != ESP_OK) {
        ESP_LOGE(TAG_VFD, "Fallo al inicializar Modbus");
        g_vfd_status = VFD_STATUS_FAULT;
        return;
    }

    // Tarea que gestiona el envío periódico de comandos
    xTaskCreate(vfd_control_task, "vfd_control_task", VFD_TASK_STACK, NULL, VFD_TASK_PRIO, &vfd_task_handle);
    ESP_LOGI(TAG_VFD, "VFD Driver (Modbus Real) inicializado. Tarea creada.");
}

void vfd_driver_set_speed(float kph) {
    if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_target_kph = kph;
        g_emergency_stop = false; // Asumimos que fijar velocidad cancela el E-Stop
        xSemaphoreGive(vfd_mutex);
    } else {
        ESP_LOGW(TAG_VFD, "No se pudo tomar mutex para set_speed");
    }
}

void vfd_driver_emergency_stop(void) {
    if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_emergency_stop = true;
        g_target_kph = 0.0; // Forzar velocidad a 0
        xSemaphoreGive(vfd_mutex);
    }

    // Forzamos la tarea para que actúe ya
    if (vfd_task_handle) {
        xTaskNotifyGive(vfd_task_handle);
    }
}

vfd_status_t vfd_driver_get_status(void) {
    vfd_status_t status;
    if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        status = g_vfd_status;
        xSemaphoreGive(vfd_mutex);
    } else {
        status = VFD_STATUS_FAULT; // Si no podemos leer, algo va mal
    }
    return status;
}

float vfd_driver_get_target_freq_hz(void) {
    float freq;
    if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        freq = g_current_freq_hz;
        xSemaphoreGive(vfd_mutex);
    } else {
        freq = 0.0f;
    }
    return freq;
}

float vfd_driver_get_real_freq_hz(void) {
    float freq;
    if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        freq = g_vfd_real_freq_hz;
        xSemaphoreGive(vfd_mutex);
    } else {
        freq = 0.0f;
    }
    return freq;
}

// ===========================================================================
// IMPLEMENTACIÓN DE FUNCIONES PRIVADAS (MODBUS Y TAREA)
// ===========================================================================

static esp_err_t vfd_write_register(uint16_t reg_addr, uint16_t value) {
    mb_param_request_t req = {
        .slave_addr = VFD_SLAVE_ID,
        .command = MB_FUNC_WRITE_SINGLE_REGISTER,
        .reg_start = reg_addr,
        .reg_size = 1
    };

    esp_err_t err = mbc_master_send_request(master_handle, &req, &value);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_VFD, "Error al escribir en registro 0x%04X: %s", reg_addr, esp_err_to_name(err));
        if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_vfd_status = VFD_STATUS_DISCONNECTED; // Asumimos desconexión
            xSemaphoreGive(vfd_mutex);
        }
    } else {
        if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_vfd_status = VFD_STATUS_OK;
            xSemaphoreGive(vfd_mutex);
        }
    }
    return err;
}

/**
 * @brief Lee un único registro (16 bits) del VFD.
 */
static esp_err_t vfd_read_register(uint16_t reg_addr, uint16_t *out_value) {
    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Buffer para la respuesta (Modbus es Big Endian)
    uint16_t read_data_be;

    mb_param_request_t req = {
        .slave_addr = VFD_SLAVE_ID,
        .command = MB_FUNC_READ_HOLDING_REGISTERS, // Función 0x03
        .reg_start = reg_addr,
        .reg_size = 1 // Leer 1 solo registro
    };

    esp_err_t err = mbc_master_send_request(master_handle, &req, &read_data_be);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_VFD, "Error al LEER registro 0x%04X: %s", reg_addr, esp_err_to_name(err));

        // Si la comunicación falla, actualizamos el estado global
        if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_vfd_status = VFD_STATUS_DISCONNECTED;
            xSemaphoreGive(vfd_mutex);
        }
    } else {
        // Convertir de Big Endian (Modbus) a Little Endian (ESP32)
        *out_value = __builtin_bswap16(read_data_be);
        ESP_LOGD(TAG_VFD, "Lectura exitosa de registro 0x%04X: valor=0x%04X", reg_addr, *out_value);
    }

    return err;
}

static esp_err_t vfd_check_and_configure_params(void) {
    ESP_LOGI(TAG_VFD, "Configurando VFD (Fuente de Comando y Frecuencia)...");

    // Corrección según Manual: F0-01 = 2 (RS485)
    if (vfd_write_register(VFD_REG_F0_01, 2) != ESP_OK) {
        ESP_LOGE(TAG_VFD, "Error configurando F0-01 (Fuente de Comando)");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Pausa entre comandos

    // Corrección según Manual: F0-02 = 9 (Comunicación)
    if (vfd_write_register(VFD_REG_F0_02, 9) != ESP_OK) {
        ESP_LOGE(TAG_VFD, "Error configurando F0-02 (Fuente de Frecuencia)");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_VFD, "VFD configurado para control por Modbus.");
    return ESP_OK;
}

static void vfd_control_task(void *pvParameters) {
    // 1. Configurar los parámetros del VFD al arrancar
    // Esperamos hasta que la configuración sea exitosa
    while(vfd_check_and_configure_params() != ESP_OK) {
        ESP_LOGE(TAG_VFD, "Reintentando configuración del VFD en 5s...");
        if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_vfd_status = VFD_STATUS_FAULT; // Fallo de config inicial
            xSemaphoreGive(vfd_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    ESP_LOGI(TAG_VFD, "Configuración VFD exitosa. Iniciando bucle de control.");

    // 2. Bucle de control principal (MODIFICADO)
    while (1) {
        // Espera VFD_POLL_MS o una notificación de E-Stop
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VFD_POLL_MS));

        float kph;
        bool estop;

        // Copia segura de variables globales
        if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            kph = g_target_kph;
            estop = g_emergency_stop;
            xSemaphoreGive(vfd_mutex);
        } else {
            ESP_LOGW(TAG_VFD, "Task no pudo tomar mutex (lectura), saltando ciclo");
            continue;
        }

        // --- SECCIÓN DE ESCRITURA (Sin cambios) ---
        if (estop || kph < 0.5) {
            // --- PARADA ---
            vfd_write_register(VFD_REG_CONTROL, VFD_CMD_STOP);
            vTaskDelay(pdMS_TO_TICKS(10));
            vfd_write_register(VFD_REG_FREQ, 0);

            if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_current_freq_hz = 0.0f;
                xSemaphoreGive(vfd_mutex);
            }
        } else {
            // --- MARCHA ---
            // Usar constante calibrada KPH_TO_HZ_RATIO = 7.8125 (NO la fórmula antigua 3.0)
            float freq_hz = kph * KPH_TO_HZ_RATIO;
            uint16_t freq_centi_hz = (uint16_t)(freq_hz * 100.0f);

            vfd_write_register(VFD_REG_FREQ, freq_centi_hz);
            vTaskDelay(pdMS_TO_TICKS(10));
            vfd_write_register(VFD_REG_CONTROL, VFD_CMD_RUN_FWD);

            if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_current_freq_hz = freq_hz;
                xSemaphoreGive(vfd_mutex);
            }
        }

        // --- SECCIÓN DE LECTURA ---
        vTaskDelay(pdMS_TO_TICKS(20)); // Pequeña pausa entre escritura y lectura

        // Leer frecuencia real del VFD (registro 0x2103)
        uint16_t real_freq_centihz = 0;
        esp_err_t read_freq_err = vfd_read_register(VFD_REG_REAL_FREQ, &real_freq_centihz);

        if (read_freq_err == ESP_OK) {
            float real_freq_hz = real_freq_centihz / 100.0f;

            // Almacenar frecuencia real leída
            if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_vfd_real_freq_hz = real_freq_hz;
                xSemaphoreGive(vfd_mutex);
            }

            // Log periódico para debugging (cada 10 ciclos = 2 segundos)
            static uint8_t log_counter = 0;
            if (++log_counter >= 10) {
                log_counter = 0;
                ESP_LOGI(TAG_VFD, "VFD Real: %.2f Hz | Target: %.2f Hz | Speed: %.1f km/h",
                         real_freq_hz, g_current_freq_hz, kph);
            }
        }

        // Leer código de fallo (registro 0x2104)
        uint16_t fault_code = 0;
        esp_err_t read_fault_err = vfd_read_register(VFD_REG_FAULT_CODE, &fault_code);

        // Actualizar el estado global basado en la lectura
        if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (read_fault_err != ESP_OK) {
                // El estado (DISCONNECTED) ya fue fijado por vfd_read_register()
                ESP_LOGW(TAG_VFD, "No se pudo leer el estado de fallo (¿desconectado?)");
            } else {
                // La comunicación fue exitosa, analizamos el resultado
                if (fault_code != 0) {
                    ESP_LOGE(TAG_VFD, "¡FALLO VFD DETECTADO! Código: 0x%04X", fault_code);
                    g_vfd_status = VFD_STATUS_FAULT;
                } else {
                    // Comunicación OK, Sin Fallo
                    g_vfd_status = VFD_STATUS_OK;
                }
            }
            xSemaphoreGive(vfd_mutex);
        } else {
             ESP_LOGW(TAG_VFD, "Task no pudo tomar mutex (escritura estado)");
        }
    }
}

static esp_err_t vfd_modbus_init(void) {
    ESP_LOGI(TAG_VFD, "Inicializando driver Modbus Master...");

    // Configuración de los parámetros seriales
    mb_serial_opts_t serial_config = {
        .mode = MB_RTU,  // Modo RTU de esp-modbus
        .port = VFD_UART_PORT,
        .uid = VFD_SLAVE_ID,
        .response_tout_ms = 1000,
        .test_tout_us = 0,
        .baudrate = VFD_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .stop_bits = UART_STOP_BITS_1,
        .parity = VFD_PARITY
    };

    // Inicializar el maestro serial
    esp_err_t err = mbc_master_create_serial((mb_communication_info_t*)&serial_config, &master_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_VFD, "mbc_master_create_serial failed: %s", esp_err_to_name(err));
        return err;
    }

    // Iniciar el stack Modbus
    err = mbc_master_start(master_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_VFD, "mbc_master_start failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Dar tiempo a que el driver se estabilice

    ESP_LOGI(TAG_VFD, "Modbus Master inicializado en UART%d (TX:%d, RX:%d, %d bps)",
             VFD_UART_PORT, VFD_TX_PIN, VFD_RX_PIN, VFD_BAUD_RATE);
    return ESP_OK;
}
