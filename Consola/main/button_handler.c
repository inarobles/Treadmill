#include "button_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "ui.h"
#include "wifi_client.h" // For upload_to_ina/itsaso
#include "audio.h"       // For audio_play_beep()

static const char *TAG = "ButtonHandler";

// --- Definiciones para el expansor de GPIO MCP23017 ---
#define MCP23017_I2C_ADDR       0x20
#define MCP23017_REG_IODIRA     0x00
#define MCP23017_REG_IODIRB     0x01
#define MCP23017_REG_GPPUA      0x0C
#define MCP23017_REG_GPPUB      0x0D
#define MCP23017_REG_GPIOA      0x12
#define MCP23017_REG_GPIOB      0x13

// --- Mapeo de botones a los pines del MCP23017 ---
#define BUTTON_SPEED_INC_PIN    (1 << 1)
#define BUTTON_SPEED_DEC_PIN    (1 << 3)
#define BUTTON_SPEED_SET_PIN    (1 << 2)
#define BUTTON_COOLDOWN_PIN     (1 << 5)
#define BUTTON_STOP_PIN         (1 << 4) // Num 9

#define BUTTON_CLIMB_INC_PIN    (1 << 1)
#define BUTTON_CLIMB_DEC_PIN    (1 << 3)
#define BUTTON_CLIMB_SET_PIN    (1 << 2)
#define BUTTON_STOP_RESUME_PIN  (1 << 5) // Num 5
#define BUTTON_START_PIN        (1 << 4) // Num 4

#define GPIOA_BUTTON_MASK (BUTTON_SPEED_INC_PIN | BUTTON_SPEED_DEC_PIN | BUTTON_SPEED_SET_PIN | BUTTON_COOLDOWN_PIN | BUTTON_STOP_PIN)
#define GPIOB_BUTTON_MASK (BUTTON_CLIMB_INC_PIN | BUTTON_CLIMB_DEC_PIN | BUTTON_CLIMB_SET_PIN | BUTTON_STOP_RESUME_PIN | BUTTON_START_PIN)

static esp_err_t mcp23017_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t value) {
    uint8_t write_buf[] = {reg, value};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), 100);
}

static esp_err_t mcp23017_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(dev_handle, &reg, 1, value, 1, 100);
}

static void button_handler_task(void *pvParameter) {
    i2c_master_bus_handle_t bus_handle = bsp_i2c_get_handle();
    if (!bus_handle) {
        ESP_LOGE(TAG, "Fallo al obtener el handle I2C para la tarea de botones. Abortando.");
        vTaskDelete(NULL);
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MCP23017_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle;
    if (i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al añadir el MCP23017 para la tarea de botones. Abortando.");
        vTaskDelete(NULL);
        return;
    }

    mcp23017_write_reg(dev_handle, MCP23017_REG_IODIRA, GPIOA_BUTTON_MASK);
    mcp23017_write_reg(dev_handle, MCP23017_REG_GPPUA, GPIOA_BUTTON_MASK);
    mcp23017_write_reg(dev_handle, MCP23017_REG_IODIRB, GPIOB_BUTTON_MASK);
    mcp23017_write_reg(dev_handle, MCP23017_REG_GPPUB, GPIOB_BUTTON_MASK);
    ESP_LOGI(TAG, "MCP23017 configurado para los botones.");

    // Estados de botones: usamos lectura directa con debounce simple
    uint8_t porta_state = 0xFF, portb_state = 0xFF;
    uint8_t prev_porta_state = 0xFF, prev_portb_state = 0xFF;

    // Debouncing: guardamos la última lectura para confirmar
    uint8_t porta_last_read = 0xFF, portb_last_read = 0xFF;

    // Variables para controlar la pulsación larga
    uint32_t speed_inc_press_start = 0;
    uint32_t speed_dec_press_start = 0;
    uint32_t climb_inc_press_start = 0;
    uint32_t climb_dec_press_start = 0;
    bool speed_inc_repeating = false;
    bool speed_dec_repeating = false;
    bool climb_inc_repeating = false;
    bool climb_dec_repeating = false;

    // Timers de repetición separados por botón (evita acumulación de eventos)
    uint32_t speed_inc_last_repeat = 0;
    uint32_t speed_dec_last_repeat = 0;
    uint32_t climb_inc_last_repeat = 0;
    uint32_t climb_dec_last_repeat = 0;

    #define LONG_PRESS_TIME_MS 1000
    #define REPEAT_INTERVAL_MS 150  // Aproximadamente 6.7 veces por segundo

    while (1) {
        // Leer estados raw de los botones
        uint8_t porta_raw, portb_raw;
        mcp23017_read_reg(dev_handle, MCP23017_REG_GPIOA, &porta_raw);
        mcp23017_read_reg(dev_handle, MCP23017_REG_GPIOB, &portb_raw);

        // Debouncing simple: solo aceptar si 2 lecturas consecutivas son iguales
        // Con polling de 20ms, esto da 40ms de debounce efectivo
        if (porta_raw == porta_last_read) {
            porta_state = porta_raw;
        }
        if (portb_raw == portb_last_read) {
            portb_state = portb_raw;
        }
        porta_last_read = porta_raw;
        portb_last_read = portb_raw;

        // Detectar cambios desde la última vez que procesamos
        uint8_t porta_changed = porta_state ^ prev_porta_state;
        uint8_t portb_changed = portb_state ^ prev_portb_state;

        // Log STOP_RESUME button state changes
        if (portb_changed & BUTTON_STOP_RESUME_PIN) {
            ESP_LOGI(TAG, "STOP_RESUME state change: portb_raw=0x%02X, portb_last=0x%02X, portb_state=0x%02X, prev=0x%02X, changed=0x%02X",
                     portb_raw, portb_last_read, portb_state, prev_portb_state, portb_changed);
        }

        if ((portb_changed & BUTTON_CLIMB_INC_PIN) && !(portb_state & BUTTON_CLIMB_INC_PIN)) {
            ESP_LOGI(TAG, "Botón físico 1 pulsado");
        }
        if ((portb_changed & BUTTON_CLIMB_SET_PIN) && !(portb_state & BUTTON_CLIMB_SET_PIN)) {
            ESP_LOGI(TAG, "Botón físico 2 pulsado");
        }
        if ((portb_changed & BUTTON_CLIMB_DEC_PIN) && !(portb_state & BUTTON_CLIMB_DEC_PIN)) {
            ESP_LOGI(TAG, "Botón físico 3 pulsado");
        }

        if (ui_is_training_select_screen_active()) {
            // En la pantalla de selección de entrenamiento, los botones seleccionan entrenamientos
            if ((portb_changed & BUTTON_CLIMB_INC_PIN) && !(portb_state & BUTTON_CLIMB_INC_PIN)) {
                ui_select_training(1);  // Botón 1 (climb inc) -> Entrenamiento 1
            }
            if ((portb_changed & BUTTON_CLIMB_SET_PIN) && !(portb_state & BUTTON_CLIMB_SET_PIN)) {
                ui_select_training(2);  // Botón 2 (climb set) -> Entrenamiento 2
            }
            if ((portb_changed & BUTTON_CLIMB_DEC_PIN) && !(portb_state & BUTTON_CLIMB_DEC_PIN)) {
                ui_select_training(3);  // Botón 3 (climb dec) -> Entrenamiento 3
            }
            if ((portb_changed & BUTTON_START_PIN) && !(portb_state & BUTTON_START_PIN)) {
                ui_select_training(4);  // Botón 4 (start) -> Entrenamiento 4
            }
            if ((portb_changed & BUTTON_STOP_RESUME_PIN) && !(portb_state & BUTTON_STOP_RESUME_PIN)) {
                ui_select_training(5);  // Botón 5 (stop/resume) -> Entrenamiento 5
            }

            // Handle right-hand physical buttons to mirror on-screen buttons
            if ((porta_changed & BUTTON_SPEED_INC_PIN) && !(porta_state & BUTTON_SPEED_INC_PIN)) {
                // Corresponds to WIFI button
                audio_play_beep();
                ESP_LOGI(TAG, "Physical button (top-right) pressed: Opening WiFi screen.");
                ui_open_wifi_list();
            }
            if ((porta_changed & BUTTON_SPEED_SET_PIN) && !(porta_state & BUTTON_SPEED_SET_PIN)) {
                // Corresponds to BLE button, triggering its placeholder action
                audio_play_beep();
                ESP_LOGI(TAG, "Physical button (middle-right) pressed: Triggering BLE action.");
                upload_to_ina(2);
            }
            if ((porta_changed & BUTTON_SPEED_DEC_PIN) && !(porta_state & BUTTON_SPEED_DEC_PIN)) {
                // Corresponds to WAX button, triggering its placeholder action
                audio_play_beep();
                ESP_LOGI(TAG, "Physical button (bottom-right) pressed: Triggering WAX action.");
                upload_to_itsaso(3);
            }
        } else if (ui_is_main_screen_active()) {
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

            // SPEED INC - detectar inicio y fin de pulsación
            if ((porta_changed & BUTTON_SPEED_INC_PIN)) {
                if (!(porta_state & BUTTON_SPEED_INC_PIN)) {
                    // Botón presionado
                    speed_inc_press_start = current_time;
                    speed_inc_repeating = false;
                    ui_speed_inc();  // Primera pulsación inmediata
                } else {
                    // Botón liberado
                    speed_inc_press_start = 0;
                    speed_inc_repeating = false;
                }
            }
            // Repetición continua si se mantiene presionado
            if (!(porta_state & BUTTON_SPEED_INC_PIN) && speed_inc_press_start > 0) {
                if (!speed_inc_repeating && (current_time - speed_inc_press_start >= LONG_PRESS_TIME_MS)) {
                    speed_inc_repeating = true;
                    speed_inc_last_repeat = current_time;
                }
                if (speed_inc_repeating && (current_time - speed_inc_last_repeat >= REPEAT_INTERVAL_MS)) {
                    ui_speed_inc();
                    speed_inc_last_repeat = current_time;
                }
            }

            // SPEED DEC - detectar inicio y fin de pulsación
            if ((porta_changed & BUTTON_SPEED_DEC_PIN)) {
                if (!(porta_state & BUTTON_SPEED_DEC_PIN)) {
                    speed_dec_press_start = current_time;
                    speed_dec_repeating = false;
                    ui_speed_dec();
                } else {
                    speed_dec_press_start = 0;
                    speed_dec_repeating = false;
                }
            }
            if (!(porta_state & BUTTON_SPEED_DEC_PIN) && speed_dec_press_start > 0) {
                if (!speed_dec_repeating && (current_time - speed_dec_press_start >= LONG_PRESS_TIME_MS)) {
                    speed_dec_repeating = true;
                    speed_dec_last_repeat = current_time;
                }
                if (speed_dec_repeating && (current_time - speed_dec_last_repeat >= REPEAT_INTERVAL_MS)) {
                    ui_speed_dec();
                    speed_dec_last_repeat = current_time;
                }
            }

            // CLIMB INC - detectar inicio y fin de pulsación
            if ((portb_changed & BUTTON_CLIMB_INC_PIN)) {
                if (!(portb_state & BUTTON_CLIMB_INC_PIN)) {
                    climb_inc_press_start = current_time;
                    climb_inc_repeating = false;
                    ui_climb_inc();
                } else {
                    climb_inc_press_start = 0;
                    climb_inc_repeating = false;
                }
            }
            if (!(portb_state & BUTTON_CLIMB_INC_PIN) && climb_inc_press_start > 0) {
                if (!climb_inc_repeating && (current_time - climb_inc_press_start >= LONG_PRESS_TIME_MS)) {
                    climb_inc_repeating = true;
                    climb_inc_last_repeat = current_time;
                }
                if (climb_inc_repeating && (current_time - climb_inc_last_repeat >= REPEAT_INTERVAL_MS)) {
                    ui_climb_inc();
                    climb_inc_last_repeat = current_time;
                }
            }

            // CLIMB DEC - detectar inicio y fin de pulsación
            if ((portb_changed & BUTTON_CLIMB_DEC_PIN)) {
                if (!(portb_state & BUTTON_CLIMB_DEC_PIN)) {
                    climb_dec_press_start = current_time;
                    climb_dec_repeating = false;
                    ui_climb_dec();
                } else {
                    climb_dec_press_start = 0;
                    climb_dec_repeating = false;
                }
            }
            if (!(portb_state & BUTTON_CLIMB_DEC_PIN) && climb_dec_press_start > 0) {
                if (!climb_dec_repeating && (current_time - climb_dec_press_start >= LONG_PRESS_TIME_MS)) {
                    climb_dec_repeating = true;
                    climb_dec_last_repeat = current_time;
                }
                if (climb_dec_repeating && (current_time - climb_dec_last_repeat >= REPEAT_INTERVAL_MS)) {
                    ui_climb_dec();
                    climb_dec_last_repeat = current_time;
                }
            }

            // Botones sin funcionalidad de repetición
            if ((porta_changed & BUTTON_SPEED_SET_PIN) && !(porta_state & BUTTON_SPEED_SET_PIN)) {
                ui_set_speed();
            }
            if ((porta_changed & BUTTON_COOLDOWN_PIN) && !(porta_state & BUTTON_COOLDOWN_PIN)) {
                ui_weight_entry();
            }
            if ((porta_changed & BUTTON_STOP_PIN) && !(porta_state & BUTTON_STOP_PIN)) {
                ui_head_toggle();
            }
            if ((portb_changed & BUTTON_CLIMB_SET_PIN) && !(portb_state & BUTTON_CLIMB_SET_PIN)) {
                ui_set_climb();
            }
            if ((portb_changed & BUTTON_START_PIN) && !(portb_state & BUTTON_START_PIN)) {
                ui_chest_toggle();
            }
            if ((portb_changed & BUTTON_STOP_RESUME_PIN) && !(portb_state & BUTTON_STOP_RESUME_PIN)) {
                ESP_LOGI(TAG, "STOP_RESUME button pressed detected: portb_changed=0x%02X, portb_state=0x%02X, prev_portb_state=0x%02X",
                         portb_changed, portb_state, prev_portb_state);
                ui_back_to_training();
            }
        } else { // Numpad screen
            char pressed_char = 0;
            if ((porta_changed & BUTTON_SPEED_INC_PIN) && !(porta_state & BUTTON_SPEED_INC_PIN)) { pressed_char = '6'; }
            else if ((porta_changed & BUTTON_SPEED_SET_PIN) && !(porta_state & BUTTON_SPEED_SET_PIN)) { pressed_char = '7'; }
            else if ((porta_changed & BUTTON_SPEED_DEC_PIN) && !(porta_state & BUTTON_SPEED_DEC_PIN)) { pressed_char = '8'; }
            else if ((porta_changed & BUTTON_STOP_PIN) && !(porta_state & BUTTON_STOP_PIN)) { pressed_char = '9'; }
            else if ((porta_changed & BUTTON_COOLDOWN_PIN) && !(porta_state & BUTTON_COOLDOWN_PIN)) { pressed_char = '0'; }
            else if ((portb_changed & BUTTON_CLIMB_INC_PIN) && !(portb_state & BUTTON_CLIMB_INC_PIN)) { pressed_char = '1'; }
            else if ((portb_changed & BUTTON_CLIMB_SET_PIN) && !(portb_state & BUTTON_CLIMB_SET_PIN)) { pressed_char = '2'; }
            else if ((portb_changed & BUTTON_CLIMB_DEC_PIN) && !(portb_state & BUTTON_CLIMB_DEC_PIN)) { pressed_char = '3'; }
            else if ((portb_changed & BUTTON_START_PIN) && !(portb_state & BUTTON_START_PIN)) { pressed_char = '4'; }
            else if ((portb_changed & BUTTON_STOP_RESUME_PIN) && !(portb_state & BUTTON_STOP_RESUME_PIN)) { pressed_char = '5'; }

            if (pressed_char != 0) {
                if (ui_handle_numpad_press(pressed_char)) {
                    ui_confirm_set_value();
                }
            }
        }

        // Actualizar estados previos con estados estables (debounced)
        prev_porta_state = porta_state;
        prev_portb_state = portb_state;

        // Poll every 20ms (50Hz) para respuesta rápida
        // Con debouncing de 3 lecturas = 60ms de estabilización efectiva
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t button_handler_init(void) {
    if (xTaskCreate(button_handler_task, "button_handler", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button_handler_task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
