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
#include <math.h>

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
#define VFD_REG_FAULT_CODE 0x2100 // (Lectura de código de fallo, 0 = Sin fallo) - CORREGIDO SEGÚN MANUAL

// Comandos para 0x2000
#define VFD_CMD_RUN_FWD    0x0012 // Corregido según test Arduino
#define VFD_CMD_STOP       0x0001 // Corregido según test Arduino

// Registros de parámetros (para configuración inicial)
#define VFD_REG_F0_01      0x0001 // (Fuente de Comando)
#define VFD_REG_F0_02      0x0002 // (Fuente de Frecuencia)

// --- Constantes del Driver ---
// CALIBRADO con hardware real: 10.00 km/h = 78.10 Hz
// Fórmula del VFD SU300: km/h = Hz × (6.4 / 50.0) → Hz = km/h × (50.0 / 6.4)
// Por tanto: 20 km/h = 156.25 Hz (NO 60 Hz como se asumía antes)
// Ratio: Hz/km/h = 50.0/6.4 = 7.8125
#define KPH_TO_HZ_RATIO    (50.0f / 6.4f) // Calibrado 2025-11-06: 7.8125 Hz/km/h
#define VFD_MAX_FREQ_HZ    160.0f // Frecuencia máxima configurada en el VFD (parámetro F0-10)
#define VFD_TASK_STACK     4096
#define VFD_TASK_PRIO      8 // Prioridad alta para control de motor
#define VFD_POLL_MS        500 // Frecuencia de sondeo. Más bajo = más errores por ruido. 500ms es robusto.

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
static bool g_emergency_stop = false;
static float g_vfd_real_freq_hz = 0.0;  // Frecuencia real leída del VFD (0x2103)

// Control de estado de marcha (Fix 1: evitar re-enviar RUN_FWD)
static bool g_vfd_is_running = false;

// Tolerancia a errores de escritura (Fix 2: no marcar DISCONNECTED al primer fallo)
static int g_write_error_count = 0;
#define VFD_MAX_WRITE_ERRORS 3

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
        // Solo notificar a la tarea si la velocidad ha cambiado realmente
        if (fabsf(kph - g_target_kph) > 0.05f) {
        g_target_kph = kph;
            xTaskNotifyGive(vfd_task_handle); // Despertar la tarea para que actúe
            g_emergency_stop = false; // Asumimos que fijar velocidad cancela el E-Stop
        }
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
        // Fix 2: Tolerar errores puntuales por EMI. Solo marcar DISCONNECTED
        // tras VFD_MAX_WRITE_ERRORS fallos consecutivos de escritura.
        g_write_error_count++;
        ESP_LOGW(TAG_VFD, "Error escritura reg 0x%04X: %s (consecutivos: %d/%d)",
                 reg_addr, esp_err_to_name(err), g_write_error_count, VFD_MAX_WRITE_ERRORS);
        if (g_write_error_count >= VFD_MAX_WRITE_ERRORS) {
            if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_vfd_status = VFD_STATUS_DISCONNECTED;
                xSemaphoreGive(vfd_mutex);
            }
        }
    } else {
        g_write_error_count = 0;  // Reset en éxito
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
        // En un entorno con ruido, los fallos de lectura son esperables.
        // Lo tratamos como un Warning y no cambiamos el estado a DISCONNECTED.
        // El sistema seguirá funcionando con el último valor bueno leído.
        ESP_LOGW(TAG_VFD, "Fallo de lectura en registro 0x%04X: %s. (Ruido esperado)", reg_addr, esp_err_to_name(err));
        // No actualizamos g_vfd_status aquí para mantener la estabilidad.
    } else {
        // La librería esp-modbus ya realiza la conversión de endianness.
        // No es necesario hacer bswap16 manual.
        *out_value = read_data_be;
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
        // Esperar por una notificación (cambio de velocidad) o por el timeout de polling
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(VFD_POLL_MS));

        float kph_to_set = -1.0f; // Usar -1 para indicar que no hay cambio
        bool estop;

        // Copia segura de variables globales
        if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            // Comprobar si el objetivo ha cambiado desde la última vez que actuamos
            static float last_acted_kph = -1.0f;
            if (fabsf(g_target_kph - last_acted_kph) > 0.05f) {
                kph_to_set = g_target_kph;
                last_acted_kph = g_target_kph;
            }
            estop = g_emergency_stop;
            xSemaphoreGive(vfd_mutex);
        } else {
            ESP_LOGW(TAG_VFD, "Task no pudo tomar mutex (lectura), saltando ciclo");
            continue;
        }

        // --- SECCIÓN DE ESCRITURA (Solo si hay un cambio de velocidad o E-Stop) ---
        if (kph_to_set >= 0.0f || estop) {
            ESP_LOGI(TAG_VFD, "Acción requerida: kph=%.2f, estop=%d", kph_to_set, estop);
            if (estop || kph_to_set < 0.5f) {
                // --- PARADA ---
                ESP_LOGI(TAG_VFD, "Enviando comando de PARADA al VFD.");
                vfd_write_register(VFD_REG_CONTROL, VFD_CMD_STOP);
                vTaskDelay(pdMS_TO_TICKS(10));
                vfd_write_register(VFD_REG_FREQ, 0);
                g_vfd_is_running = false;  // Fix 1: Resetear estado de marcha
            } else {
                // --- MARCHA ---
                float freq_hz = kph_to_set * KPH_TO_HZ_RATIO;

                // El VFD espera un valor porcentual de la frecuencia máxima, no un valor absoluto en Hz.
                // La escala es 10000 para 100.00%.
                // Fórmula: (frecuencia_deseada / frecuencia_maxima) * 10000
                uint16_t vfd_value = (uint16_t)((freq_hz / VFD_MAX_FREQ_HZ) * 10000.0f);

                // Fix 1: Solo enviar frecuencia. Enviar RUN_FWD únicamente la
                // primera vez (transición parado→marcha). Re-enviar RUN_FWD con
                // el motor ya corriendo causa un "hiccup" en muchos VFDs.
                vfd_write_register(VFD_REG_FREQ, vfd_value);

                if (!g_vfd_is_running) {
                    ESP_LOGI(TAG_VFD, "Primera marcha: %.2f Hz (VFD: %u) — enviando RUN_FWD", freq_hz, vfd_value);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    vfd_write_register(VFD_REG_CONTROL, VFD_CMD_RUN_FWD);
                    g_vfd_is_running = true;
                } else {
                    ESP_LOGI(TAG_VFD, "Ajuste de frecuencia: %.2f Hz (VFD: %u) — motor ya en marcha", freq_hz, vfd_value);
                }
            }
        }

        // --- SECCIÓN DE LECTURA (Se ejecuta periódicamente) ---
        vTaskDelay(pdMS_TO_TICKS(50)); // Pequeña pausa para no saturar

        // Leer frecuencia real del VFD
        uint16_t real_freq_centihz = 0;
        if (vfd_read_register(VFD_REG_REAL_FREQ, &real_freq_centihz) == ESP_OK) {
            if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_vfd_real_freq_hz = real_freq_centihz / 100.0f;
                xSemaphoreGive(vfd_mutex);
            }
        }

        // Leer estado de fallo del VFD
        uint16_t fault_code = 0;
        if (vfd_read_register(VFD_REG_FAULT_CODE, &fault_code) == ESP_OK) {
            if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (fault_code != 0) {
                    // Manejo de fallos (simplificado para claridad)
                    ESP_LOGE(TAG_VFD, "¡FALLO VFD DETECTADO! Código: 0x%04X", fault_code);
                    g_vfd_status = VFD_STATUS_FAULT;
                } else {
                    if (g_vfd_status == VFD_STATUS_FAULT) {
                        ESP_LOGI(TAG_VFD, "Fallo VFD resuelto.");
                    }
                    g_vfd_status = VFD_STATUS_OK;
                }
                xSemaphoreGive(vfd_mutex);
            }
        }
    }
}

float vfd_driver_get_real_freq_hz(void) {
    float freq = 0.0f;
    if (xSemaphoreTake(vfd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        freq = g_vfd_real_freq_hz;
        xSemaphoreGive(vfd_mutex);
    }
    return freq;
}

static esp_err_t vfd_modbus_init(void) {
    ESP_LOGI(TAG_VFD, "Inicializando driver Modbus Master...");

    // Configuración de los parámetros seriales para Modbus
    mb_serial_opts_t serial_config = {
        .mode = MB_RTU,
        .port = VFD_UART_PORT,
        .uid = VFD_SLAVE_ID,
        .response_tout_ms = 1000,
        .test_tout_us = 0,
        .baudrate = VFD_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .stop_bits = UART_STOP_BITS_1,
        .parity = VFD_PARITY
    };

    // Inicializar el maestro serial usando la API correcta para esta versión
    esp_err_t err = mbc_master_create_serial((mb_communication_info_t*)&serial_config, &master_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_VFD, "mbc_master_create_serial failed: %s", esp_err_to_name(err));
        return err;
    }

    // Configurar los pines UART para el puerto Modbus DESPUÉS de crear el maestro.
    // Es crucial establecer RTS en UART_PIN_NO_CHANGE para hardware con control automático de dirección.
    ESP_ERROR_CHECK(uart_set_pin(VFD_UART_PORT, VFD_TX_PIN, VFD_RX_PIN, VFD_RTS_PIN, UART_PIN_NO_CHANGE));

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
