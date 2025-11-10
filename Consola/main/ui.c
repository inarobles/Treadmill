#include "ui.h"
#include "audio.h"
#include "wifi_client.h"
#include "wifi_manager.h"
#include "treadmill_state.h"
#include "ble_client.h"
#include "cm_master.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "nvs_flash.h"
#include "nvs.h"

LV_FONT_DECLARE(chivo_mono_70);
LV_FONT_DECLARE(chivo_mono_100);

const char *TAG = "UI";  // Accesible desde ui_wifi.c
// UI functions from ui_wifi.c are declared in ui.h




const float COOLDOWN_RAMP_RATE_KMH_S = 10.0f / 120.0f; // Rampa lenta de 2 minutos para el cool down
const float STOP_RAMP_RATE_KMH_S = 5.0f;    // Rampa rápida para detener/reanudar

//==================================================================================
// 1B. FUNCIONES DE PERSISTENCIA (NVS)
//==================================================================================

#define NVS_NAMESPACE_WAX "wax_maintenance"
#define NVS_KEY_TOTAL_SECONDS "total_seconds"

/**
 * @brief Carga el contador de horas de cera desde NVS
 * @return Segundos acumulados, o 0 si no hay datos guardados
 */
static uint32_t load_wax_counter_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_WAX, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error opening NVS for wax counter read: %s", esp_err_to_name(err));
        return 0;
    }

    uint32_t total_seconds = 0;
    err = nvs_get_u32(nvs_handle, NVS_KEY_TOTAL_SECONDS, &total_seconds);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Wax counter not found in NVS, starting at 0");
        total_seconds = 0;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error reading wax counter from NVS: %s", esp_err_to_name(err));
        total_seconds = 0;
    } else {
        ESP_LOGI(TAG, "Loaded wax counter from NVS: %lu seconds", total_seconds);
    }

    nvs_close(nvs_handle);
    return total_seconds;
}

/**
 * @brief Guarda el contador de horas de cera en NVS
 * @param total_seconds Segundos acumulados a guardar
 */
static void save_wax_counter_to_nvs(uint32_t total_seconds) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_WAX, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS for wax counter write: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u32(nvs_handle, NVS_KEY_TOTAL_SECONDS, total_seconds);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing wax counter to NVS: %s", esp_err_to_name(err));
    } else {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error committing wax counter to NVS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Saved wax counter to NVS: %lu seconds", total_seconds);
        }
    }

    nvs_close(nvs_handle);
}

//==================================================================================
// 2. PUNTEROS GLOBALES A OBJETOS LVGL
//==================================================================================
// -- Pantalla de Selección de Entrenamiento --
lv_obj_t *scr_training_select;  // Accesible desde ui_wifi.c
static lv_obj_t *btn_training_itsaso;
static lv_obj_t *label_training_itsaso;
static lv_obj_t *btn_training_ina;
static lv_obj_t *label_training_ina;
static lv_timer_t *wifi_check_timer;

// -- Pantalla de escaneo BLE --
static lv_obj_t *scr_ble_scan;
static lv_obj_t *list_ble_devices;
static lv_obj_t *spinner_ble_scan;

// -- Pantalla de Carga --
static lv_obj_t *scr_loading;

// -- Pantalla de Subida --
static lv_obj_t *scr_uploading;

// -- Pantalla de Apagado --
static lv_obj_t *scr_shutdown;

// -- Pantalla Principal --
static lv_obj_t *scr_main;
static lv_obj_t *label_dist;
static lv_obj_t *label_time;
static lv_obj_t *label_climb_percent;
static lv_obj_t *label_speed_kmh;
static lv_obj_t *label_speed_pace;
static lv_obj_t *label_pulse;
static lv_obj_t *label_kcal;
static lv_obj_t *unit_kcal_main;  // Label de unidad "Kcal" en pantalla MAIN
static lv_obj_t *unit_kcal_set;  // Label de unidad "Kcal" en pantalla SET
static lv_obj_t *label_stop_btn;
static lv_obj_t *label_cooldown_btn;
static lv_obj_t *btn_stop;
static lv_obj_t *btn_cooldown;
static lv_obj_t *btn_upload_training;
static lv_obj_t *ta_info;
// Botones de velocidad e inclinación (para deshabilitación visual)
static lv_obj_t *btn_speed_inc;
static lv_obj_t *btn_speed_set;
static lv_obj_t *btn_speed_dec;
static lv_obj_t *btn_climb_inc;
static lv_obj_t *btn_climb_set;
static lv_obj_t *btn_climb_dec;
static lv_timer_t *text_area_timer;
static lv_obj_t *label_chest_value;
static lv_obj_t *label_head_value;
static int chest_value = 0;
static int head_value = 0;
static bool need_restore_weight_buttons = false;
static bool buttons_are_stop_mode = false;
static bool showing_weight_in_kcal_field = false;

// -- Pantalla de Ajuste (Clon) --
static lv_obj_t *scr_set;
static lv_obj_t *scr_wax;
static lv_obj_t *label_wax_hours;
static lv_obj_t *btn_apply_wax;
static lv_obj_t *btn_wax_back;
static lv_obj_t *label_dist_set;
static lv_obj_t *label_time_set;
static lv_obj_t *label_climb_percent_set;
static lv_obj_t *label_speed_kmh_set;
static lv_obj_t *label_speed_pace_set;
static lv_obj_t *label_pulse_set;
static lv_obj_t *label_kcal_set;
static lv_obj_t *ta_info_set;

// WiFi screens are now handled in ui_wifi.c

//==================================================================================
// ESTRUCTURA PARA PANELES COMUNES
//==================================================================================
typedef struct {
    lv_obj_t *dist_label;
    lv_obj_t *time_label;
    lv_obj_t *climb_percent_label;
    lv_obj_t *speed_kmh_label;
    lv_obj_t *speed_pace_label;
    lv_obj_t *pulse_label;
    lv_obj_t *kcal_label;
    lv_obj_t *info_label;
} UIPanels;

//==================================================================================
// 3. DECLARACIONES DE FUNCIONES
//==================================================================================
static void set_info_text(const char *text);
static void set_info_text_persistent(const char *text);
static void text_area_clear_timer_cb(lv_timer_t *timer);
static void ble_device_btn_delete_cb(lv_event_t *e);
static void create_main_screen(void);
static void create_set_screen(void);
static void create_wax_screen(void);
static void _switch_to_set_screen_internal(set_mode_t mode);
static void _switch_to_main_screen_internal(void);
static void _update_set_display_text_internal(void);
static bool _handle_numpad_press_internal(char digit);
static void wifi_check_timer_cb(lv_timer_t *timer);
static void weight_event_cb(lv_event_t *e);
static void stop_resume_event_cb(lv_event_t *e);
static void back_to_training_select_event_cb(lv_event_t *e);
static void cool_down_event_cb(lv_event_t *e);
static void end_event_cb(lv_event_t *e);
static void wifi_selector_event_cb(lv_event_t *e);
static void wax_event_cb(lv_event_t *e);
static void apply_wax_event_cb(lv_event_t *e);
static void wax_back_event_cb(lv_event_t *e);


//==================================================================================
// 4. TAREA PRINCIPAL DE ACTUALIZACIÓN
//==================================================================================
static void text_area_clear_timer_cb(lv_timer_t *timer) {
    lv_label_set_text(ta_info, "");
    text_area_timer = NULL; // The timer is deleted automatically, just clear the handle.
}

static uint32_t wifi_connected_timestamp = 0;

static void wifi_check_timer_cb(lv_timer_t *timer) {
    bool internet_connected = is_internet_connected();

    if (internet_connected) {
        // Habilitar botones y cambiar texto
        if (btn_training_itsaso && label_training_itsaso) {
            lv_obj_clear_state(btn_training_itsaso, LV_STATE_DISABLED);
            lv_label_set_text(label_training_itsaso, "2 - Entrenamiento Itsaso");
        }
        if (btn_training_ina && label_training_ina) {
            lv_obj_clear_state(btn_training_ina, LV_STATE_DISABLED);
            lv_label_set_text(label_training_ina, "3 - Entrenamiento Ina");
        }
    } else {
        // Mantener botones deshabilitados
        if (btn_training_itsaso && label_training_itsaso) {
            lv_obj_add_state(btn_training_itsaso, LV_STATE_DISABLED);
            lv_label_set_text(label_training_itsaso, "Estableciendo conexion a internet...");
        }
        if (btn_training_ina && label_training_ina) {
            lv_obj_add_state(btn_training_ina, LV_STATE_DISABLED);
            lv_label_set_text(label_training_ina, "Estableciendo conexion a internet...");
        }
    }
}

void set_info_text_persistent(const char *text) {
    lv_label_set_text(ta_info, text);
    if (text_area_timer) {
        lv_timer_del(text_area_timer);
        text_area_timer = NULL;
    }
}

void set_info_text(const char *text) {
    lv_label_set_text(ta_info, text);
    // If a timer is already running, delete it before creating a new one.
    if (text_area_timer) {
        lv_timer_del(text_area_timer);
    }
    // Create a new one-shot timer.
    text_area_timer = lv_timer_create(text_area_clear_timer_cb, 10000, NULL);
    if (text_area_timer) { // Good practice to check for null
        lv_timer_set_repeat_count(text_area_timer, 1);
    }
}

void ui_update_task(void *pvParameter) {
    const uint32_t UI_UPDATE_INTERVAL_MS = 100; // 10Hz
    // const float NORMAL_RAMP_RATE_KMH_S = 5.0f;   // Aceleración/deceleración normal
    uint32_t time_ms_counter = 0;
    static bool was_stopped = true;
    // Variables para evitar parpadeos (solo actualizar si cambian)
    static int prev_speed_int = -1;
    static int prev_speed_frac = -1;
    static int prev_climb_int = -1;
    static uint32_t prev_elapsed_seconds = 0xFFFFFFFF;
    static int prev_dist_value = -1;
    static bool prev_dist_is_meters = true;
    static int prev_pace_int = -1;
    static int prev_pace_frac = -1;
    static int prev_pulse = -1;
    static bool prev_pulse_connected = false;
    static int prev_kcal = -1;

    while (1) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);

        // --- Actualizar velocidad e inclinación reales desde el esclavo (via RS485) ---
        // Los valores reales vienen del esclavo, no se simulan localmente
        float real_speed_from_slave = cm_master_get_real_speed();
        float real_incline_from_slave = cm_master_get_current_incline();
        g_treadmill_state.speed_kmh = real_speed_from_slave;

        // Actualizar inclinación real, excepto en modo cooldown donde se gestiona localmente
        if (g_treadmill_state.ramp_mode != RAMP_MODE_COOLDOWN_STOP) {
            g_treadmill_state.climb_percent = real_incline_from_slave;
        }

        // --- Actualizar estados de ventiladores desde el esclavo ---
        uint8_t head_fan_from_slave = cm_master_get_head_fan_state();
        uint8_t chest_fan_from_slave = cm_master_get_chest_fan_state();

        // Actualizar valores locales
        head_value = head_fan_from_slave;
        chest_value = chest_fan_from_slave;

        // Detectar cambio de modo de rampa si alcanzamos target
        float speed_diff = g_treadmill_state.target_speed - g_treadmill_state.speed_kmh;
        if (fabs(speed_diff) < 0.05f) {
            if (g_treadmill_state.ramp_mode != RAMP_MODE_NORMAL) {
                g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
                g_treadmill_state.is_resuming = false;
            }
        }

        // --- Lógica de Rampa de Velocidad (Cool Down) ---
        // En modo cooldown, reducir la velocidad gradualmente
        static uint32_t last_cooldown_speed_update_ms = 0;
        if (g_treadmill_state.ramp_mode == RAMP_MODE_COOLDOWN_STOP && g_treadmill_state.target_speed == 0.0f) {
            // Enviar comandos de velocidad periódicamente durante el cooldown
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (now_ms - last_cooldown_speed_update_ms >= 500) {  // Cada 500ms
                last_cooldown_speed_update_ms = now_ms;
                // Calcular velocidad objetivo actual basada en la rampa de cooldown
                float current_speed = g_treadmill_state.speed_kmh;
                if (current_speed > 0.1f) {
                    float speed_decrement = COOLDOWN_RAMP_RATE_KMH_S * 0.5f;  // 500ms de decremento
                    float new_target = current_speed - speed_decrement;
                    if (new_target < 0) new_target = 0;
                    cm_master_set_speed(new_target);
                }
            }
        }

        // --- Lógica de Rampa de Inclinación (Cool Down) ---
        // En modo cooldown, la inclinación se gestiona localmente
        if (g_treadmill_state.ramp_mode == RAMP_MODE_COOLDOWN_STOP && g_treadmill_state.climb_percent > 0) {
            float decrement = g_treadmill_state.cooldown_climb_ramp_rate * (UI_UPDATE_INTERVAL_MS / 1000.0f);
            g_treadmill_state.climb_percent -= decrement;
            if (g_treadmill_state.climb_percent < 0) {
                g_treadmill_state.climb_percent = 0;
            }
            // Enviar comando al esclavo con la nueva inclinación
            cm_master_set_incline(g_treadmill_state.climb_percent);
        }

        // --- Time and Data updates ---
        if (g_treadmill_state.speed_kmh > 0.0f) {
            if (was_stopped) {
                if (g_treadmill_state.selected_training == 1 && !g_treadmill_state.has_shown_welcome_message) {
                    set_info_text("Que tengas un buen entreno!");
                    g_treadmill_state.has_shown_welcome_message = true;
                }
                was_stopped = false;
                // Cambiar botones a STOP y COOL DOWN cuando la cinta empieza a moverse
                need_restore_weight_buttons = true;
            }
            // Hide upload button if treadmill starts moving again
            lv_obj_add_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
            time_ms_counter += UI_UPDATE_INTERVAL_MS;
            if (time_ms_counter >= 1000) {
                time_ms_counter -= 1000;
                g_treadmill_state.elapsed_seconds++;
                // Acumular tiempo total de uso para el contador de cera
                g_treadmill_state.total_running_seconds++;

                // Guardar el contador de cera en NVS cada 60 segundos
                static uint32_t nvs_save_counter = 0;
                nvs_save_counter++;
                if (nvs_save_counter >= 60) {
                    nvs_save_counter = 0;
                    save_wax_counter_to_nvs(g_treadmill_state.total_running_seconds);
                }
            }
            // Check if minimum running time has been reached (10 seconds)
            if (!g_treadmill_state.has_run_minimum_time && g_treadmill_state.elapsed_seconds >= 10) {
                g_treadmill_state.has_run_minimum_time = true;
            }
            double distance_this_interval = (double)g_treadmill_state.speed_kmh / 3600.0 * (UI_UPDATE_INTERVAL_MS / 1000.0);
            g_treadmill_state.total_distance_km += distance_this_interval;

            // Calcular calorías usando la fórmula ACSM (solo si se ha introducido el peso)
            // kcal = [ (0.2 × velocidad (m/min) + 0.9 × velocidad (m/min) × pendiente (decimal) + 3.5) × peso (kg) × tiempo (min) ] ÷ 200
            if (g_treadmill_state.weight_entered) {
                float speed_m_min = g_treadmill_state.speed_kmh * 1000.0f / 60.0f;  // Convertir km/h a m/min
                float slope_decimal = g_treadmill_state.climb_percent / 100.0f;      // Convertir % a decimal
                float time_min = (UI_UPDATE_INTERVAL_MS / 1000.0f) / 60.0f;         // Tiempo en minutos para este intervalo

                float kcal_this_interval = ((0.2f * speed_m_min + 0.9f * speed_m_min * slope_decimal + 3.5f)
                                            * g_treadmill_state.user_weight_kg * time_min) / 200.0f;
                g_treadmill_state.sim_kcal += kcal_this_interval;
            }
            // Si no se ha introducido el peso, las kcal permanecen en 0
        } else {
            // Speed is zero - show upload button if conditions are met
            if (g_treadmill_state.has_run_minimum_time &&
                !g_treadmill_state.has_uploaded &&
                (g_treadmill_state.selected_training == 2 || g_treadmill_state.selected_training == 3)) {
                lv_obj_clear_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
                set_info_text_persistent("Pulsa UPLOAD para terminar el ejercicio y enviarlo a tu entrenador.");
            }
            was_stopped = true;
            time_ms_counter = 0;
        }

        // Copiar todos los valores necesarios a variables locales ANTES de liberar el mutex
        uint32_t elapsed_seconds_copy = g_treadmill_state.elapsed_seconds;
        double total_distance_km_copy = g_treadmill_state.total_distance_km;
        float speed_kmh_copy = g_treadmill_state.speed_kmh;
        float climb_percent_copy = g_treadmill_state.climb_percent;
        bool ble_connected_copy = g_treadmill_state.ble_connected;
        int real_pulse_copy = g_treadmill_state.real_pulse;
        bool weight_entered_copy = g_treadmill_state.weight_entered;
        float user_weight_kg_copy = g_treadmill_state.user_weight_kg;
        set_mode_t set_mode_copy = g_treadmill_state.set_mode;
        int current_kcal = (int)(g_treadmill_state.sim_kcal + 0.5f);
        uint8_t head_fan_copy = head_value;
        uint8_t chest_fan_copy = chest_value;

        xSemaphoreGive(g_state_mutex);

        // --- Actualizaciones de UI con variables locales (fuera de la sección crítica) ---

        // Time
        if (elapsed_seconds_copy != prev_elapsed_seconds) {
            uint32_t hours = elapsed_seconds_copy / 3600;
            uint32_t minutes = (elapsed_seconds_copy % 3600) / 60;
            uint32_t seconds = elapsed_seconds_copy % 60;
            lv_label_set_text_fmt(label_time, "%"PRIu32":%02"PRIu32":%02"PRIu32, hours, minutes, seconds);
            lv_label_set_text_fmt(label_time_set, "%"PRIu32":%02"PRIu32":%02"PRIu32, hours, minutes, seconds);
            prev_elapsed_seconds = elapsed_seconds_copy;
        }

        // Distance
        bool is_meters = total_distance_km_copy < 1.0;
        int dist_value;
        if (is_meters) {
            dist_value = (int)(total_distance_km_copy * 1000);
        } else {
            int dist_int = (int)total_distance_km_copy;
            int dist_frac = (int)fabs((total_distance_km_copy - dist_int) * 1000);
            dist_value = dist_int * 1000 + dist_frac;
        }

        if (dist_value != prev_dist_value || is_meters != prev_dist_is_meters) {
            // TEMPORAL: valor de prueba
            lv_label_set_text_fmt(label_dist, "%d", 10000);
            lv_label_set_text_fmt(label_dist_set, "%d", 10000);
            /*
            if (is_meters) {
                int meters = (int)(total_distance_km_copy * 1000);
                lv_label_set_text_fmt(label_dist, "%d", meters);
                lv_label_set_text_fmt(label_dist_set, "%d", meters);
            } else {
                int dist_int = (int)total_distance_km_copy;
                int dist_frac = (int)fabs((total_distance_km_copy - dist_int) * 1000);
                lv_label_set_text_fmt(label_dist, "%d.%03d", dist_int, dist_frac);
                lv_label_set_text_fmt(label_dist_set, "%d.%03d", dist_int, dist_frac);
            }
            */
            prev_dist_value = dist_value;
            prev_dist_is_meters = is_meters;
        }

        // Speed
        int total_speed_tenths = (int)roundf(speed_kmh_copy * 10.0f);
        int speed_int = total_speed_tenths / 10;
        int speed_frac = total_speed_tenths % 10;
        if (speed_int != prev_speed_int || speed_frac != prev_speed_frac) {
            lv_label_set_text_fmt(label_speed_kmh, "%d.%d", speed_int, speed_frac);
            if (set_mode_copy != SET_MODE_SPEED && set_mode_copy != SET_MODE_WEIGHT) {
                lv_label_set_text_fmt(label_speed_kmh_set, "%d.%d", speed_int, speed_frac);
            }
            prev_speed_int = speed_int;
            prev_speed_frac = speed_frac;
        }

        // Climb percent
        int climb_int = (int)roundf(climb_percent_copy);
        if (climb_int != prev_climb_int) {
            lv_label_set_text_fmt(label_climb_percent, "%d", climb_int);
            if (set_mode_copy != SET_MODE_CLIMB && set_mode_copy != SET_MODE_WEIGHT) {
                lv_label_set_text_fmt(label_climb_percent_set, "%d", climb_int);
            }
            prev_climb_int = climb_int;
        }

        // Pace
        int pace_int, pace_frac;
        if (speed_kmh_copy > 0.1f) {
            float pace_in_minutes = 60.0f / speed_kmh_copy;
            if (pace_in_minutes > 99.9f) pace_in_minutes = 99.9f;
            int total_tenths = (int)roundf(pace_in_minutes * 10.0f);
            pace_int = total_tenths / 10;
            pace_frac = total_tenths % 10;
        } else {
            pace_int = 0;
            pace_frac = 0;
        }

        if (pace_int != prev_pace_int || pace_frac != prev_pace_frac) {
            if (speed_kmh_copy > 0.1f) {
                lv_label_set_text_fmt(label_speed_pace, "%d.%d", pace_int, pace_frac);
                if (set_mode_copy != SET_MODE_SPEED) {
                    lv_label_set_text_fmt(label_speed_pace_set, "%d.%d", pace_int, pace_frac);
                }
            } else {
                lv_label_set_text(label_speed_pace, "0.0");
                if (set_mode_copy != SET_MODE_SPEED) lv_label_set_text(label_speed_pace_set, "0.0");
            }
            prev_pace_int = pace_int;
            prev_pace_frac = pace_frac;
        }

        // Pulse
        int current_pulse_copy = (ble_connected_copy && real_pulse_copy > 0) ? real_pulse_copy : -1;
        bool pulse_connected_copy = ble_connected_copy && real_pulse_copy > 0;

        if (current_pulse_copy != prev_pulse || pulse_connected_copy != prev_pulse_connected) {
            if (pulse_connected_copy) {
                lv_label_set_text_fmt(label_pulse, "%d", real_pulse_copy);
                lv_label_set_text_fmt(label_pulse_set, "%d", real_pulse_copy);
            } else {
                lv_label_set_text(label_pulse, "---");
                lv_label_set_text(label_pulse_set, "---");
            }
            prev_pulse = current_pulse_copy;
            prev_pulse_connected = pulse_connected_copy;
        }

        // Kcal
        if (current_kcal != prev_kcal) {
            // TEMPORAL: valor de prueba
            lv_label_set_text_fmt(label_kcal, "%d", 1000);
            lv_label_set_text_fmt(label_kcal_set, "%d", 1000);
            /*
            if (weight_entered_copy || user_weight_kg_copy == 0.0f) {
                lv_label_set_text_fmt(label_kcal, "%d", current_kcal);
            }
            if (set_mode_copy != SET_MODE_WEIGHT) {
                lv_label_set_text_fmt(label_kcal_set, "%d", current_kcal);
            }
            */
            prev_kcal = current_kcal;
        }

        // Actualizar labels de ventiladores
        static int prev_head_fan = -1;
        static int prev_chest_fan = -1;
        if (head_fan_copy != prev_head_fan) {
            char buf[2];
            sprintf(buf, "%d", head_fan_copy);
            lv_label_set_text(label_head_value, buf);
            prev_head_fan = head_fan_copy;
        }
        if (chest_fan_copy != prev_chest_fan) {
            char buf[2];
            sprintf(buf, "%d", chest_fan_copy);
            lv_label_set_text(label_chest_value, buf);
            prev_chest_fan = chest_fan_copy;
        }

        xSemaphoreGive(g_state_mutex);

        // Cambiar botones a STOP/COOL DOWN cuando la cinta se mueve (solo una vez)
        if (need_restore_weight_buttons) {
            lv_label_set_text(label_stop_btn, "STOP");
            lv_label_set_text(label_cooldown_btn, "COOL\nDOWN");
            // Cambiar callbacks
            lv_obj_remove_event_cb(btn_stop, back_to_training_select_event_cb);
            lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_remove_event_cb(btn_cooldown, weight_event_cb);
            lv_obj_add_event_cb(btn_cooldown, cool_down_event_cb, LV_EVENT_CLICKED, NULL);
            buttons_are_stop_mode = true;
            need_restore_weight_buttons = false;
            // Marcar que el peso ya ha sido introducido ahora que la cinta se está moviendo
            g_treadmill_state.weight_entered = true;
            // Cambiar la unidad de "kg" a "Kcal" ahora que empezamos a contar calorías
            if (showing_weight_in_kcal_field) {
                lv_label_set_text(unit_kcal_main, "Kcal");  // Cambiar unidad en pantalla MAIN
                lv_label_set_text(label_kcal, "0");  // Cambiar el peso por 0 Kcal
                showing_weight_in_kcal_field = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(UI_UPDATE_INTERVAL_MS));
    }
}

//==================================================================================
// 5. ACTUALIZACIÓN VISUAL DE BOTONES
//==================================================================================

// Actualiza la opacidad de los botones +/- y SET según el estado RESUME
static void update_button_states_visual(void) {
    // Leer estado actual con protección de mutex
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool is_stopped = g_treadmill_state.is_stopped;
    bool is_cooling_down = g_treadmill_state.is_cooling_down;
    xSemaphoreGive(g_state_mutex);

    // Si hay algún RESUME activo (STOP o COOL DOWN), deshabilitar visualmente +/-, SET
    if (is_stopped || is_cooling_down) {
        // Reducir opacidad al 30% para botones deshabilitados
        lv_obj_set_style_opa(btn_speed_inc, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_speed_set, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_speed_dec, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_climb_inc, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_climb_set, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_climb_dec, LV_OPA_30, 0);

        // NO reducir opacidad de los botones END - deben verse completamente opacos en rojo
        // Solo deshabilitar el botón contrario (el que no es END)
        // El botón END mantiene su opacidad completa para que se vea el rojo brillante
    } else {
        // Restaurar opacidad completa
        lv_obj_set_style_opa(btn_speed_inc, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(btn_speed_set, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(btn_speed_dec, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(btn_climb_inc, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(btn_climb_set, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(btn_climb_dec, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(btn_stop, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(btn_cooldown, LV_OPA_COVER, 0);
    }
}

//==================================================================================
// 6. MANEJORES DE EVENTOS
//==================================================================================

static void speed_inc_event_cb(lv_event_t *e) {
    ui_speed_inc();
}

static void speed_dec_event_cb(lv_event_t *e) {
    ui_speed_dec();
}

static void climb_inc_event_cb(lv_event_t *e) {
    ui_climb_inc();
}

static void climb_dec_event_cb(lv_event_t *e) {
    ui_climb_dec();
}

static void stop_resume_event_cb(lv_event_t *e) {
    static TickType_t last_click_tick = 0;
    TickType_t current_tick = xTaskGetTickCount();
    const TickType_t debounce_ticks = pdMS_TO_TICKS(200); // 200ms debounce

    if (current_tick - last_click_tick < debounce_ticks) {
        ESP_LOGW(TAG, "stop_resume_event_cb: IGNORADO (debounce), tick=%lu, delta=%lu ms",
                 (unsigned long)current_tick, (unsigned long)((current_tick - last_click_tick) * portTICK_PERIOD_MS));
        return;
    }

    last_click_tick = current_tick;
    ESP_LOGI(TAG, "stop_resume_event_cb: touchscreen button clicked, tick=%lu", (unsigned long)current_tick);
    ui_stop_resume();
}

static void cool_down_event_cb(lv_event_t *e) {
    static TickType_t last_click_tick = 0;
    TickType_t current_tick = xTaskGetTickCount();
    const TickType_t debounce_ticks = pdMS_TO_TICKS(200); // 200ms debounce

    if (current_tick - last_click_tick < debounce_ticks) {
        ESP_LOGW(TAG, "cool_down_event_cb: IGNORADO (debounce), tick=%lu, delta=%lu ms",
                 (unsigned long)current_tick, (unsigned long)((current_tick - last_click_tick) * portTICK_PERIOD_MS));
        return;
    }

    last_click_tick = current_tick;
    ESP_LOGI(TAG, "cool_down_event_cb: touchscreen button clicked, tick=%lu", (unsigned long)current_tick);
    ui_cool_down();
}

static void end_event_cb(lv_event_t *e) {
    static TickType_t last_click_tick = 0;
    TickType_t current_tick = xTaskGetTickCount();
    const TickType_t debounce_ticks = pdMS_TO_TICKS(200); // 200ms debounce

    if (current_tick - last_click_tick < debounce_ticks) {
        ESP_LOGW(TAG, "end_event_cb: IGNORADO (debounce), tick=%lu, delta=%lu ms",
                 (unsigned long)current_tick, (unsigned long)((current_tick - last_click_tick) * portTICK_PERIOD_MS));
        return;
    }

    last_click_tick = current_tick;
    ESP_LOGI(TAG, "end_event_cb: touchscreen button clicked, tick=%lu", (unsigned long)current_tick);

    audio_play_beep();

    // Limpiar timer de WiFi si existe
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    // Resetear timestamp WiFi
    wifi_connected_timestamp = 0;

    // Verificar si se debe mostrar el botón UPLOAD al volver a la pantalla inicial
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool should_show_upload = g_treadmill_state.has_run_minimum_time &&
                              !g_treadmill_state.has_uploaded &&
                              (g_treadmill_state.selected_training == 2 || g_treadmill_state.selected_training == 3);
    xSemaphoreGive(g_state_mutex);

    if (should_show_upload) {
        lv_obj_clear_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Mostrando botón UPLOAD en pantalla inicial (desde END)");
    } else {
        lv_obj_add_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
    }

    // Volver a la pantalla de selección de entrenamiento
    lv_scr_load(scr_training_select);

    // Recrear timer WiFi
    wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 100, NULL);
}

static void upload_training_event_cb(lv_event_t *e) {
    audio_play_beep();

    if (!is_wifi_connected()) {
        set_info_text("WiFi no conectado. No se puede subir.");
        return;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    int training = g_treadmill_state.selected_training;

    // Obtener los datos del entrenamiento
    double distance_km = g_treadmill_state.total_distance_km;
    uint32_t total_seconds = g_treadmill_state.elapsed_seconds;

    xSemaphoreGive(g_state_mutex);

    // Calcular distancia en metros
    int distance_m = (int)(distance_km * 1000);

    // Calcular tiempo en formato H:MM:SS
    uint32_t hours = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;

    // Crear el mensaje a enviar
    char upload_data[256];
    snprintf(upload_data, sizeof(upload_data),
             "Distancia recorrida: %dm, Tiempo empleado: %u:%02u:%02u",
             distance_m, hours, minutes, seconds);

    // Cambiar a pantalla de subida
    lv_scr_load(scr_uploading);

    // Subir datos al servidor de Oracle Cloud
    if (training == 2) {
        // Entrenamiento Itsaso
        upload_to_oracle_itsaso(upload_data);
        ESP_LOGI(TAG, "Uploading to Oracle for Itsaso: %s", upload_data);
    } else if (training == 3) {
        // Entrenamiento Ina
        upload_to_oracle_ina(upload_data);
        ESP_LOGI(TAG, "Uploading to Oracle for Ina: %s", upload_data);
    }
}

static void set_speed_event_cb(lv_event_t *e) {
    ui_set_speed();
}

static void set_climb_event_cb(lv_event_t *e) {
    ui_set_climb();
}

static void numpad_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    if (ui_handle_numpad_press(txt[0])) {
        ui_confirm_set_value();
    }
}

//==================================================================================
// 6. FUNCIONES DE CREACIÓN DE INTERFAZ
//==================================================================================
static lv_style_t style_title, style_value_main, style_value_secondary, style_unit, style_value_extra_large, style_btn_symbol, style_btn_text;
static void create_styles(void) {
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_40);
    lv_style_set_text_color(&style_title, lv_color_hex(0x000000));

    lv_style_init(&style_value_secondary);
    lv_style_set_text_font(&style_value_secondary, &lv_font_montserrat_40);

    lv_style_init(&style_unit);
    lv_style_set_text_font(&style_unit, &lv_font_montserrat_18);
    lv_style_set_text_color(&style_unit, lv_color_hex(0x888888));

    lv_style_init(&style_btn_symbol);
    lv_style_set_text_font(&style_btn_symbol, &lv_font_montserrat_44);

    lv_style_init(&style_btn_text);
    lv_style_set_text_font(&style_btn_text, &lv_font_montserrat_24);
    
    lv_style_init(&style_value_extra_large);
    lv_style_set_text_font(&style_value_extra_large, &chivo_mono_100); 

    lv_style_init(&style_value_main);
    lv_style_set_text_font(&style_value_main, &chivo_mono_70);
}

static UIPanels create_common_ui_elements(lv_obj_t *parent) {
    UIPanels panels;

    // TIME (principal)
    panels.time_label = lv_label_create(parent);
    lv_obj_add_style(panels.time_label, &style_value_extra_large, 0);
    lv_obj_align(panels.time_label, LV_ALIGN_CENTER, 0, -110);

    lv_obj_t* unit_time = lv_label_create(parent);
    lv_obj_add_style(unit_time, &style_unit, 0);
    lv_label_set_text(unit_time, "Time");
    lv_obj_align_to(unit_time, panels.time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    // KCAL (izquierda, encima de las horas de Time)
    panels.kcal_label = lv_label_create(parent);
    lv_obj_add_style(panels.kcal_label, &style_value_main, 0);
    lv_obj_align_to(panels.kcal_label, panels.time_label, LV_ALIGN_OUT_TOP_LEFT, -160, -40);
    lv_obj_set_width(panels.kcal_label, 120);
    lv_obj_set_style_text_align(panels.kcal_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_kcal = lv_label_create(parent);
    lv_obj_add_style(unit_kcal, &style_unit, 0);
    lv_label_set_text(unit_kcal, "Kcal");
    lv_obj_align_to(unit_kcal, panels.kcal_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_kcal, 120);
    lv_obj_set_style_text_align(unit_kcal, LV_TEXT_ALIGN_RIGHT, 0);

    // Guardar referencias según la pantalla
    if (parent == scr_main) {
        unit_kcal_main = unit_kcal;
    } else {
        unit_kcal_set = unit_kcal;
    }

    // DISTANCE (derecha, encima de los segundos de Time)
    panels.dist_label = lv_label_create(parent);
    lv_obj_add_style(panels.dist_label, &style_value_main, 0);
    lv_obj_align_to(panels.dist_label, panels.time_label, LV_ALIGN_OUT_TOP_RIGHT, 102, -40);
    lv_obj_set_width(panels.dist_label, 150);
    lv_obj_set_style_text_align(panels.dist_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_dist = lv_label_create(parent);
    lv_obj_add_style(unit_dist, &style_unit, 0);
    lv_label_set_text(unit_dist, "Distance");
    lv_obj_align_to(unit_dist, panels.dist_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_dist, 150);
    lv_obj_set_style_text_align(unit_dist, LV_TEXT_ALIGN_RIGHT, 0);

    // --- COLUMNA DE INCLINACIÓN (CLIMB) ---
    lv_obj_t *label_climb_title = lv_label_create(parent);
    lv_obj_add_style(label_climb_title, &style_title, 0);
    lv_obj_set_style_text_color(label_climb_title, lv_color_hex(0xFF0000), 0);
    lv_label_set_text(label_climb_title, "CLIMB");
    lv_obj_align(label_climb_title, LV_ALIGN_TOP_LEFT, 150, 90);
    lv_obj_set_width(label_climb_title, 180);
    lv_obj_set_style_text_align(label_climb_title, LV_TEXT_ALIGN_RIGHT, 0);
    
    panels.climb_percent_label = lv_label_create(parent);
    lv_obj_add_style(panels.climb_percent_label, &style_value_main, 0);
    lv_obj_align_to(panels.climb_percent_label, label_climb_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_width(panels.climb_percent_label, 180);
    lv_obj_set_style_text_align(panels.climb_percent_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_percent = lv_label_create(parent);
    lv_obj_add_style(unit_percent, &style_unit, 0);
    lv_label_set_text(unit_percent, "Percent");
    lv_obj_align_to(unit_percent, panels.climb_percent_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_percent, 180);
    lv_obj_set_style_text_align(unit_percent, LV_TEXT_ALIGN_RIGHT, 0);
    
    panels.pulse_label = lv_label_create(parent);
    lv_obj_add_style(panels.pulse_label, &style_value_main, 0);
    lv_label_set_text(panels.pulse_label, "---");  // Texto inicial cuando no hay sensor
    lv_obj_align_to(panels.pulse_label, unit_percent, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_width(panels.pulse_label, 180);
    lv_obj_set_style_text_align(panels.pulse_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_pulse = lv_label_create(parent);
    lv_obj_add_style(unit_pulse, &style_unit, 0);
    lv_label_set_text(unit_pulse, "Pulse");
    lv_obj_align_to(unit_pulse, panels.pulse_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_pulse, 180);
    lv_obj_set_style_text_align(unit_pulse, LV_TEXT_ALIGN_RIGHT, 0);

    // --- COLUMNA DE VELOCIDAD (SPEED) ---
    lv_obj_t *label_speed_title = lv_label_create(parent);
    lv_obj_add_style(label_speed_title, &style_title, 0);
    lv_obj_set_style_text_color(label_speed_title, lv_color_hex(0x00A000), 0);
    lv_label_set_text(label_speed_title, "SPEED");
    lv_obj_align(label_speed_title, LV_ALIGN_TOP_RIGHT, -200, 90);
    lv_obj_set_width(label_speed_title, 180);
    lv_obj_set_style_text_align(label_speed_title, LV_TEXT_ALIGN_RIGHT, 0);

    panels.speed_kmh_label = lv_label_create(parent);
    lv_obj_add_style(panels.speed_kmh_label, &style_value_main, 0);
    lv_obj_align_to(panels.speed_kmh_label, label_speed_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_width(panels.speed_kmh_label, 180);
    lv_obj_set_style_text_align(panels.speed_kmh_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_kmh = lv_label_create(parent);
    lv_obj_add_style(unit_kmh, &style_unit, 0);
    lv_label_set_text(unit_kmh, "km/h");
    lv_obj_align_to(unit_kmh, panels.speed_kmh_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_kmh, 180);
    lv_obj_set_style_text_align(unit_kmh, LV_TEXT_ALIGN_RIGHT, 0);

    panels.speed_pace_label = lv_label_create(parent);
    lv_obj_add_style(panels.speed_pace_label, &style_value_main, 0);
    lv_obj_align_to(panels.speed_pace_label, unit_kmh, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_width(panels.speed_pace_label, 180);
    lv_obj_set_style_text_align(panels.speed_pace_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_pace = lv_label_create(parent);
    lv_obj_add_style(unit_pace, &style_unit, 0);
    lv_label_set_text(unit_pace, "min/km");
    lv_obj_align_to(unit_pace, panels.speed_pace_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_pace, 180);
    lv_obj_set_style_text_align(unit_pace, LV_TEXT_ALIGN_RIGHT, 0);

    // --- INFO BOX (MODIFIED FROM TEXTAREA TO LABEL) ---
    panels.info_label = lv_label_create(parent);
    lv_obj_add_style(panels.info_label, &style_title, 0);
    lv_obj_set_size(panels.info_label, 820, 190);
    lv_obj_align(panels.info_label, LV_ALIGN_CENTER, 0, 90);
    lv_obj_set_style_text_align(panels.info_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_all(panels.info_label, 10, 0);
    lv_obj_set_style_bg_color(panels.info_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(panels.info_label, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panels.info_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(panels.info_label, 2, 0);
    return panels;
}

//==================================================================================
// FUNCIONES Y CALLBACKS PARA ESCANEO BLE
//==================================================================================

/**
 * @brief Event handler for the 'Back' button on the BLE scan screen.
 */
static void ble_scan_back_event_cb(lv_event_t *e) {
    audio_play_beep();

    // Try to reconnect to the saved device when exiting without selecting one
    ble_addr_t saved_addr;
    if (ble_client_load_saved_device(&saved_addr)) {
        ESP_LOGI(TAG, "Reconnecting to previously saved device...");
        ble_client_connect(saved_addr);
    }

    // Note: The scan stops on its own after a timeout.
    lv_scr_load(scr_training_select);
}

/**
 * @brief Event handler for selecting a device from the BLE list.
 */
static void ble_device_select_event_cb(lv_event_t *e) {
    audio_play_beep();
    lv_obj_t *btn = lv_event_get_target(e);
    ble_addr_t *addr = (ble_addr_t *)lv_obj_get_user_data(btn);

    if (addr) {
        ESP_LOGI(TAG, "Device selected. Saving and connecting...");
        ble_client_save_device(*addr);
        ble_client_connect(*addr);
        lv_scr_load(scr_training_select); // Go back to the main selection screen
    }
}

/**
 * @brief Callback function passed to the BLE client to add devices to the UI list.
 * @note This function can be called from a different task, so UI operations must be locked.
 */
static void ui_add_ble_device_to_list(const char* name, ble_addr_t addr) {
    // The spinner is hidden when the first device is found.
    if (!lv_obj_has_flag(spinner_ble_scan, LV_OBJ_FLAG_HIDDEN)) {
        bsp_display_lock(0);
        lv_obj_add_flag(spinner_ble_scan, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
    }

    bsp_display_lock(0);

    // Allocate memory for the address on the heap, so it persists after the scan.
    ble_addr_t *addr_copy = malloc(sizeof(ble_addr_t));
    if (addr_copy) {
        memcpy(addr_copy, &addr, sizeof(ble_addr_t));

        lv_obj_t *btn = lv_list_add_btn(list_ble_devices, LV_SYMBOL_BLUETOOTH, name);
        lv_obj_set_user_data(btn, addr_copy); // Attach the heap-allocated address
        lv_obj_add_event_cb(btn, ble_device_select_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(btn, ble_device_btn_delete_cb, LV_EVENT_DELETE, NULL);  // Free memory on delete
    }

    bsp_display_unlock();
}

/**
 * @brief Callback to free allocated memory when a BLE device button is deleted
 */
static void ble_device_btn_delete_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    ble_addr_t *addr = (ble_addr_t *)lv_obj_get_user_data(btn);
    if (addr) {
        free(addr);
        lv_obj_set_user_data(btn, NULL);
    }
}

/**
 * @brief Event handler for the main 'BLE' button. Starts the scan.
 */
static void ble_scan_button_event_cb(lv_event_t *e) {
    audio_play_beep();
    
    bsp_display_lock(0);
    // Clear any old items from the list
    lv_obj_clean(list_ble_devices);
    // Show the spinner
    lv_obj_clear_flag(spinner_ble_scan, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();

    // Start the scan, passing the UI update function as a callback
    ble_client_start_scan(ui_add_ble_device_to_list);
    
    // Switch to the scan screen
    lv_scr_load(scr_ble_scan);
}


/**
 * @brief Creates the BLE device scanning and selection screen.
 */
static void create_ble_scan_screen(void) {
    scr_ble_scan = lv_obj_create(NULL);
    lv_obj_set_size(scr_ble_scan, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_ble_scan, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *label_title = lv_label_create(scr_ble_scan);
    lv_obj_add_style(label_title, &style_title, 0);
    lv_label_set_text(label_title, "Buscando Sensores de Pulso");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);

    // Device List
    list_ble_devices = lv_list_create(scr_ble_scan);
    lv_obj_set_size(list_ble_devices, 600, 600);
    lv_obj_align(list_ble_devices, LV_ALIGN_CENTER, 0, 20);

    // Spinner
    spinner_ble_scan = lv_spinner_create(scr_ble_scan, 1000, 60);
    lv_obj_set_size(spinner_ble_scan, 100, 100);
    lv_obj_align(spinner_ble_scan, LV_ALIGN_CENTER, 0, 20);
    // Spinner is shown by default, hidden when first device is found.

    // Back Button
    lv_obj_t *btn_back = lv_btn_create(scr_ble_scan);
    lv_obj_set_size(btn_back, 150, 50);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_back, ble_scan_back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "Volver");
    lv_obj_center(label_back);
}


// CALLBACKS PARA PANTALLA DE SELECCIÓN DE ENTRENAMIENTO
//==================================================================================
/*
static void upload_event_cb(lv_event_t *e) {
    audio_play_beep();
    lv_obj_t *btn = lv_event_get_target(e);
    int number = (int)lv_obj_get_user_data(btn);
    ESP_LOGI(TAG, "Upload button %d pressed", number);

    if (number <= 2) {
        upload_to_ina(number);
    } else {
        upload_to_itsaso(number);
    }
}
*/

static void chest_event_cb(lv_event_t *e) {
    ui_chest_toggle();
}

static void head_event_cb(lv_event_t *e) {
    ui_head_toggle();
}

static void weight_event_cb(lv_event_t *e) {
    audio_play_beep();
    _switch_to_set_screen_internal(SET_MODE_WEIGHT);
    lv_scr_load(scr_set);
}

static void back_to_training_select_event_cb(lv_event_t *e) {
    audio_play_beep();
    // Limpiar timer de WiFi si existe
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }
    // Resetear timestamp WiFi
    wifi_connected_timestamp = 0;

    // Verificar si se debe mostrar el botón UPLOAD al volver a la pantalla inicial
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool should_show_upload = g_treadmill_state.has_run_minimum_time &&
                              !g_treadmill_state.has_uploaded &&
                              (g_treadmill_state.selected_training == 2 || g_treadmill_state.selected_training == 3);
    xSemaphoreGive(g_state_mutex);

    if (should_show_upload) {
        lv_obj_clear_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Mostrando botón UPLOAD en pantalla inicial");
    } else {
        lv_obj_add_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
    }

    lv_scr_load(scr_training_select);
    // Recrear timer WiFi
    wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 100, NULL);
}

static void training_free_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Entrenamiento libre seleccionado");

    // Limpiar timer de WiFi
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 1;
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);
    lv_scr_load(scr_main);
    set_info_text_persistent("Selecciona una velocidad para comenzar");
}

static void training_itsaso_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Entrenamiento Itsaso seleccionado - iniciando descarga");

    // Limpiar timer de WiFi
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 2;
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);
    lv_scr_load(scr_loading);
    wifi_download_plan("itsaso");
}

static void training_ina_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Entrenamiento Ina seleccionado - iniciando descarga");

    // Limpiar timer de WiFi
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 3;
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);
    lv_scr_load(scr_loading);
    wifi_download_plan("ina");
}

static void training_alain_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Entrenamiento Alain seleccionado");

    // Limpiar timer de WiFi
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 4;
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);
    lv_scr_load(scr_main);
    set_info_text_persistent("Los enanos tienen que usar esta cinta con supervision de aita o ama.");
}

static void training_urko_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Entrenamiento Urko seleccionado");

    // Limpiar timer de WiFi
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = 5;
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    xSemaphoreGive(g_state_mutex);
    lv_scr_load(scr_main);
    set_info_text_persistent("Los enanos tienen que usar esta cinta con supervision de aita o ama.");
}

//==================================================================================
// CREACIÓN DE PANTALLA DE SELECCIÓN
//==================================================================================
static void create_training_select_screen(void) {
    scr_training_select = lv_obj_create(NULL);
    lv_obj_set_size(scr_training_select, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_training_select, LV_OBJ_FLAG_SCROLLABLE);

    // Botones anchos pegados a la izquierda (igual que pantalla principal)
    const int left_btn_w = 500;
    const int btn_h = 136;
    const int margin = 20;
    lv_obj_t *btn, *l;

    // Contenedor pegado a la izquierda (igual que left_col en pantalla principal)
    lv_obj_t *btn_container = lv_obj_create(scr_training_select);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, left_btn_w, LV_PCT(100));
    lv_obj_align(btn_container, LV_ALIGN_TOP_LEFT, margin, 0);
    lv_obj_set_layout(btn_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(btn_container, margin, 0);
    lv_obj_set_style_pad_bottom(btn_container, margin, 0);

    // Botón 1: Entrenamiento libre
    btn = lv_btn_create(btn_container);
    lv_obj_set_size(btn, left_btn_w, btn_h);
    lv_obj_add_event_cb(btn, training_free_event_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "1 - Entrenamiento libre");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 20, 0);

    // Botón 2: Entrenamiento Itsaso
    btn_training_itsaso = lv_btn_create(btn_container);
    lv_obj_set_size(btn_training_itsaso, left_btn_w, btn_h);
    lv_obj_add_event_cb(btn_training_itsaso, training_itsaso_event_cb, LV_EVENT_CLICKED, NULL);
    label_training_itsaso = lv_label_create(btn_training_itsaso);
    lv_obj_add_style(label_training_itsaso, &style_btn_text, 0);
    lv_label_set_text(label_training_itsaso, "Estableciendo conexion a internet...");
    lv_obj_align(label_training_itsaso, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_add_state(btn_training_itsaso, LV_STATE_DISABLED);  // Inicialmente deshabilitado

    // Botón 3: Entrenamiento Ina
    btn_training_ina = lv_btn_create(btn_container);
    lv_obj_set_size(btn_training_ina, left_btn_w, btn_h);
    lv_obj_add_event_cb(btn_training_ina, training_ina_event_cb, LV_EVENT_CLICKED, NULL);
    label_training_ina = lv_label_create(btn_training_ina);
    lv_obj_add_style(label_training_ina, &style_btn_text, 0);
    lv_label_set_text(label_training_ina, "Estableciendo conexion a internet...");
    lv_obj_align(label_training_ina, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_add_state(btn_training_ina, LV_STATE_DISABLED);  // Inicialmente deshabilitado

    // Botón 4: Entrenamiento Alain
    btn = lv_btn_create(btn_container);
    lv_obj_set_size(btn, left_btn_w, btn_h);
    lv_obj_add_event_cb(btn, training_alain_event_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "4 - Entrenamiento Alain");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 20, 0);

    // Botón 5: Entrenamiento Urko
    btn = lv_btn_create(btn_container);
    lv_obj_set_size(btn, left_btn_w, btn_h);
    lv_obj_add_event_cb(btn, training_urko_event_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "5 - Entrenamiento Urko");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 20, 0);

    // --- NEW: Right column for numbered buttons ---
    const int right_btn_w = 120;
    lv_obj_t * right_col = lv_obj_create(scr_training_select);
    lv_obj_remove_style_all(right_col);
    lv_obj_set_size(right_col, right_btn_w, LV_PCT(100));
    lv_obj_align(right_col, LV_ALIGN_TOP_RIGHT, -margin, 0);
    lv_obj_set_layout(right_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    // Align items to the top to leave empty space at the bottom
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(right_col, margin, 0);
    lv_obj_set_style_pad_bottom(right_col, margin, 0);
    lv_obj_set_style_pad_gap(right_col, 20, 0); // Add a small gap between buttons

    // Create only the 3 required buttons
    for (int i = 1; i <= 3; i++) {
        btn = lv_btn_create(right_col);
        lv_obj_set_size(btn, right_btn_w, btn_h);
        lv_obj_set_user_data(btn, (void*)i);

        // Set event callbacks
        if (i == 1) {
            lv_obj_add_event_cb(btn, wifi_selector_event_cb, LV_EVENT_CLICKED, NULL);
        } else if (i == 2) {
            lv_obj_add_event_cb(btn, ble_scan_button_event_cb, LV_EVENT_CLICKED, NULL);
        } else {
            // WAX button
            lv_obj_add_event_cb(btn, wax_event_cb, LV_EVENT_CLICKED, NULL);
        }

        l = lv_label_create(btn);
        lv_obj_add_style(l, &style_btn_text, 0);

        // Set labels
        if (i == 1) {
            lv_label_set_text(l, "WIFI");
        } else if (i == 2) {
            lv_label_set_text(l, "BLE");
        } else { // i == 3
            lv_label_set_text(l, "WAX");
        }
        lv_obj_center(l);
    }

    // Botón UPLOAD (solo visible para Itsaso e Ina)
    btn_upload_training = lv_btn_create(right_col);
    lv_obj_set_size(btn_upload_training, right_btn_w, btn_h);
    lv_obj_add_event_cb(btn_upload_training, upload_training_event_cb, LV_EVENT_CLICKED, NULL);
    // Aplicar color rojo al botón
    lv_obj_set_style_bg_color(btn_upload_training, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    l = lv_label_create(btn_upload_training);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "UPLOAD");
    lv_obj_center(l);
    // Ocultar por defecto (solo se muestra para entrenamientos 2 y 3)
    lv_obj_add_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);

    // Crear timer para verificar estado de WiFi cada 100ms
    wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 100, NULL);
}

//==================================================================================
// CREACIÓN DE PANTALLA DE CARGA
//==================================================================================
static void create_loading_screen(void) {
    scr_loading = lv_obj_create(NULL);
    lv_obj_set_size(scr_loading, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_loading, LV_OBJ_FLAG_SCROLLABLE);

    // Fondo negro
    lv_obj_set_style_bg_color(scr_loading, lv_color_black(), 0);

    // Mensaje centrado
    lv_obj_t *label = lv_label_create(scr_loading);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_label_set_text(label, "Recibiendo tu\nentrenamiento\npersonalizado");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

static void create_uploading_screen(void) {
    scr_uploading = lv_obj_create(NULL);
    lv_obj_set_size(scr_uploading, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_uploading, LV_OBJ_FLAG_SCROLLABLE);

    // Fondo negro
    lv_obj_set_style_bg_color(scr_uploading, lv_color_black(), 0);

    // Mensaje centrado
    lv_obj_t *label = lv_label_create(scr_uploading);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_label_set_text(label, "Tu entrenamiento se esta\nenviando a tu entrenador\npersonal");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

static void create_main_screen(void) {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_size(scr_main, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);
    
    UIPanels panels = create_common_ui_elements(scr_main);
    label_dist = panels.dist_label;
    label_time = panels.time_label;
    label_climb_percent = panels.climb_percent_label;
    label_speed_kmh = panels.speed_kmh_label;
    label_speed_pace = panels.speed_pace_label;
    label_pulse = panels.pulse_label;
    label_kcal = panels.kcal_label;
    ta_info = panels.info_label;

    const int btn_w = 120, btn_h = 136;
    const int margin = 20;
    lv_obj_t *btn, *l;

    lv_obj_t * left_col = lv_obj_create(scr_main);
    lv_obj_remove_style_all(left_col);
    lv_obj_set_size(left_col, btn_w, LV_PCT(100));
    lv_obj_align(left_col, LV_ALIGN_TOP_LEFT, margin, 0);
    lv_obj_set_layout(left_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(left_col, margin, 0);
    lv_obj_set_style_pad_bottom(left_col, margin, 0);

    btn_climb_inc = lv_btn_create(left_col); lv_obj_set_size(btn_climb_inc, btn_w, btn_h); lv_obj_add_event_cb(btn_climb_inc, climb_inc_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_climb_inc); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_PLUS); lv_obj_center(l);
    btn_climb_set = lv_btn_create(left_col); lv_obj_set_size(btn_climb_set, btn_w, btn_h); lv_obj_add_event_cb(btn_climb_set, set_climb_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_climb_set); lv_obj_add_style(l, &style_btn_text, 0); lv_label_set_text(l, "SET"); lv_obj_center(l);
    btn_climb_dec = lv_btn_create(left_col); lv_obj_set_size(btn_climb_dec, btn_w, btn_h); lv_obj_add_event_cb(btn_climb_dec, climb_dec_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_climb_dec); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_MINUS); lv_obj_center(l);

    // Botón CHEST
    btn = lv_btn_create(left_col);
    lv_obj_set_size(btn, btn_w, btn_h);
    lv_obj_add_event_cb(btn, chest_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "CHEST\nFAN");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón
    label_chest_value = lv_label_create(btn);
    lv_obj_add_style(label_chest_value, &style_btn_text, 0);
    lv_label_set_text(label_chest_value, "0");
    lv_obj_add_flag(label_chest_value, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón

    btn_stop = lv_btn_create(left_col); lv_obj_set_size(btn_stop, btn_w, btn_h);
    label_stop_btn = lv_label_create(btn_stop);
    lv_obj_add_style(label_stop_btn, &style_btn_text, 0);
    lv_label_set_text(label_stop_btn, "STOP");
    lv_obj_center(label_stop_btn);
    // Callback se añadirá después según weight_entered

    lv_obj_t * right_col = lv_obj_create(scr_main);
    lv_obj_remove_style_all(right_col);
    lv_obj_set_size(right_col, btn_w, LV_PCT(100));
    lv_obj_align(right_col, LV_ALIGN_TOP_RIGHT, -margin, 0);
    lv_obj_set_layout(right_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(right_col, margin, 0);
    lv_obj_set_style_pad_bottom(right_col, margin, 0);
    
    btn_speed_inc = lv_btn_create(right_col); lv_obj_set_size(btn_speed_inc, btn_w, btn_h); lv_obj_add_event_cb(btn_speed_inc, speed_inc_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_speed_inc); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_PLUS); lv_obj_center(l);
    btn_speed_set = lv_btn_create(right_col); lv_obj_set_size(btn_speed_set, btn_w, btn_h); lv_obj_add_event_cb(btn_speed_set, set_speed_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_speed_set); lv_obj_add_style(l, &style_btn_text, 0); lv_label_set_text(l, "SET"); lv_obj_center(l);
    btn_speed_dec = lv_btn_create(right_col); lv_obj_set_size(btn_speed_dec, btn_w, btn_h); lv_obj_add_event_cb(btn_speed_dec, speed_dec_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_speed_dec); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_MINUS); lv_obj_center(l);

    // Botón HEAD
    btn = lv_btn_create(right_col);
    lv_obj_set_size(btn, btn_w, btn_h);
    lv_obj_add_event_cb(btn, head_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "HEAD\nFAN");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón
    label_head_value = lv_label_create(btn);
    lv_obj_add_style(label_head_value, &style_btn_text, 0);
    lv_label_set_text(label_head_value, "0");
    lv_obj_add_flag(label_head_value, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón

    btn_cooldown = lv_btn_create(right_col); lv_obj_set_size(btn_cooldown, btn_w, btn_h);
    label_cooldown_btn = lv_label_create(btn_cooldown);
    lv_obj_add_style(label_cooldown_btn, &style_btn_text, 0);
    lv_label_set_text(label_cooldown_btn, "COOL\nDOWN");
    lv_obj_set_style_text_align(label_cooldown_btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_cooldown_btn);

    // Siempre empezar con BACK (izquierda) y WEIGHT (derecha)
    lv_label_set_text(label_stop_btn, "BACK");
    lv_label_set_text(label_cooldown_btn, "WEIGHT");
    lv_obj_add_event_cb(btn_stop, back_to_training_select_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_cooldown, weight_event_cb, LV_EVENT_CLICKED, NULL);

    // Inicializar estado visual de botones (al inicio todos deben estar habilitados)
    update_button_states_visual();
}

static void create_set_screen(void) {
    scr_set = lv_obj_create(NULL);
    lv_obj_set_size(scr_set, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_set, LV_OBJ_FLAG_SCROLLABLE);
    
    UIPanels panels = create_common_ui_elements(scr_set);
    label_dist_set = panels.dist_label;
    label_time_set = panels.time_label;
    label_climb_percent_set = panels.climb_percent_label;
    label_speed_kmh_set = panels.speed_kmh_label;
    label_speed_pace_set = panels.speed_pace_label;
    label_pulse_set = panels.pulse_label;
    label_kcal_set = panels.kcal_label;
    ta_info_set = panels.info_label;

    // --- Creación del teclado numérico ---
    const int btn_w = 120, btn_h = 136;
    const int margin = 20;
    lv_obj_t *btn, *l;
    char buf[2];

    lv_obj_t * left_col = lv_obj_create(scr_set);
    lv_obj_remove_style_all(left_col);
    lv_obj_set_size(left_col, btn_w, LV_PCT(100));
    lv_obj_align(left_col, LV_ALIGN_TOP_LEFT, margin, 0);
    lv_obj_set_layout(left_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(left_col, margin, 0);

    for (int i = 1; i <= 5; i++) {
        btn = lv_btn_create(left_col);
        lv_obj_set_size(btn, btn_w, btn_h);
        l = lv_label_create(btn);
        lv_obj_add_style(l, &style_btn_symbol, 0);
        sprintf(buf, "%d", i);
        lv_label_set_text(l, buf);
        lv_obj_center(l);
        lv_obj_add_event_cb(btn, numpad_event_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t * right_col = lv_obj_create(scr_set);
    lv_obj_remove_style_all(right_col);
    lv_obj_set_size(right_col, btn_w, LV_PCT(100));
    lv_obj_align(right_col, LV_ALIGN_TOP_RIGHT, -margin, 0);
    lv_obj_set_layout(right_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(right_col, margin, 0);

    for (int i = 6; i <= 10; i++) {
        int num = (i == 10) ? 0 : i;
        btn = lv_btn_create(right_col);
        lv_obj_set_size(btn, btn_w, btn_h);
        l = lv_label_create(btn);
        lv_obj_add_style(l, &style_btn_symbol, 0);
        sprintf(buf, "%d", num);
        lv_label_set_text(l, buf);
        lv_obj_center(l);
        lv_obj_add_event_cb(btn, numpad_event_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void create_wax_screen(void) {
    scr_wax = lv_obj_create(NULL);
    lv_obj_set_size(scr_wax, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_wax, LV_OBJ_FLAG_SCROLLABLE);

    // Título
    lv_obj_t *title = lv_label_create(scr_wax);
    lv_obj_add_style(title, &style_title, 0);
    lv_label_set_text(title, "WAX MAINTENANCE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Mensaje informativo
    lv_obj_t *info = lv_label_create(scr_wax);
    lv_obj_add_style(info, &style_btn_text, 0);
    lv_label_set_text(info, "Running hours since last wax:");
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    // Label para mostrar las horas
    label_wax_hours = lv_label_create(scr_wax);
    lv_obj_add_style(label_wax_hours, &style_value_main, 0);
    lv_label_set_text(label_wax_hours, "0:00");
    lv_obj_align(label_wax_hours, LV_ALIGN_CENTER, 0, 0);

    // Botón APPLY WAX
    btn_apply_wax = lv_btn_create(scr_wax);
    lv_obj_set_size(btn_apply_wax, 300, 100);
    lv_obj_align(btn_apply_wax, LV_ALIGN_CENTER, 0, 120);
    lv_obj_add_event_cb(btn_apply_wax, apply_wax_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn_apply_wax);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "APPLY WAX");
    lv_obj_center(l);

    // Botón Volver
    btn_wax_back = lv_btn_create(scr_wax);
    lv_obj_set_size(btn_wax_back, 150, 50);
    lv_obj_align(btn_wax_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_wax_back, wax_back_event_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(btn_wax_back);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_label_set_text(l, "Volver");
    lv_obj_center(l);
}

static void create_shutdown_screen(void) {
    scr_shutdown = lv_obj_create(NULL);
    lv_obj_set_size(scr_shutdown, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_shutdown, LV_OBJ_FLAG_SCROLLABLE);

    // Fondo negro
    lv_obj_set_style_bg_color(scr_shutdown, lv_color_black(), 0);

    // Mensaje centrado
    lv_obj_t *label = lv_label_create(scr_shutdown);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_label_set_text(label, "Entrenamiento enviado con éxito,\npuedes apagar la cinta con seguridad.");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}


//==================================================================================
// 7. FUNCIONES DE GESTIÓN DE PANTALLA Y CURSOR
//==================================================================================
static void _update_set_display_text_internal(void) {
    char display_buf[10];

    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT || g_treadmill_state.set_mode == SET_MODE_CLIMB) {
        // Para peso e inclinación usamos 2 dígitos (decenas y unidades)
        char d1 = (g_treadmill_state.set_digit_index > 0) ? g_treadmill_state.set_buffer[0] : '-';
        char d2 = (g_treadmill_state.set_digit_index > 1) ? g_treadmill_state.set_buffer[1] : '-';

        char cursor = g_treadmill_state.blink_state ? '-' : ' ';
        if (g_treadmill_state.set_digit_index == 0) d1 = cursor;
        else if (g_treadmill_state.set_digit_index == 1) d2 = cursor;

        sprintf(display_buf, "%c%c", d1, d2);

        if (g_treadmill_state.set_mode == SET_MODE_WEIGHT) {
            lv_label_set_text(label_kcal_set, display_buf);  // Mostramos el peso en el campo de kcal
        } else {
            lv_label_set_text(label_climb_percent_set, display_buf);  // Mostramos inclinación
        }
    } else {
        // Para velocidad usamos 3 dígitos con punto decimal
        char d1 = (g_treadmill_state.set_digit_index > 0) ? g_treadmill_state.set_buffer[0] : '-';
        char d2 = (g_treadmill_state.set_digit_index > 1) ? g_treadmill_state.set_buffer[1] : '-';
        char d3 = (g_treadmill_state.set_digit_index > 2) ? g_treadmill_state.set_buffer[2] : '-';

        char cursor = g_treadmill_state.blink_state ? '-' : ' ';
        if (g_treadmill_state.set_digit_index == 0) d1 = cursor;
        else if (g_treadmill_state.set_digit_index == 1) d2 = cursor;
        else if (g_treadmill_state.set_digit_index == 2) d3 = cursor;

        sprintf(display_buf, "%c%c.%c", d1, d2, d3);
        lv_label_set_text(label_speed_kmh_set, display_buf);
    }
}

static void blink_timer_cb(lv_timer_t *timer) {
    g_treadmill_state.blink_state = !g_treadmill_state.blink_state;
    _update_set_display_text_internal();
}

static void _switch_to_set_screen_internal(set_mode_t mode) {
    g_treadmill_state.set_mode = mode;
    g_treadmill_state.set_digit_index = 0;
    g_treadmill_state.set_buffer[0] = '\0';
    g_treadmill_state.blink_state = true;
    if (!g_treadmill_state.blink_timer) {
        g_treadmill_state.blink_timer = lv_timer_create(blink_timer_cb, 500, NULL);
    }
    _update_set_display_text_internal();

    if (mode == SET_MODE_SPEED) {
        lv_label_set_text(ta_info_set, "Seleccione la velocidad deseada.");
        int climb_int = (int)roundf(g_treadmill_state.climb_percent);
        lv_label_set_text_fmt(label_climb_percent_set, "%d", climb_int);
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_CLIMB) {
        lv_label_set_text(ta_info_set, "Seleccione la inclinacion deseada (2 digitos).");
        int speed_int = (int)g_treadmill_state.speed_kmh;
        int speed_frac = (int)fabs((g_treadmill_state.speed_kmh - speed_int) * 10);
        lv_label_set_text_fmt(label_speed_kmh_set, "%d.%d", speed_int, speed_frac);
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_WEIGHT) {
        lv_label_set_text(ta_info_set, "Introduce tu peso (2 digitos: decenas y unidades).");
        // El peso se muestra en label_kcal_set mediante _update_set_display_text_internal()
        // Cambiar la unidad a "kg"
        lv_label_set_text(unit_kcal_set, "kg");
    }
}

static void _switch_to_main_screen_internal(void) {
    g_treadmill_state.set_mode = SET_MODE_NONE;
    if (g_treadmill_state.blink_timer) {
        lv_timer_del(g_treadmill_state.blink_timer);
        g_treadmill_state.blink_timer = NULL;
    }
    // Restaurar la unidad a "Kcal" solo si NO estamos mostrando el peso (usar unit_kcal_main para pantalla MAIN)
    if (!showing_weight_in_kcal_field) {
        lv_label_set_text(unit_kcal_main, "Kcal");
    }

    // Forzar actualización de los displays con los valores actuales confirmados
    int speed_int = (int)g_treadmill_state.speed_kmh;
    int speed_frac = (int)fabs((g_treadmill_state.speed_kmh - speed_int) * 10);
    lv_label_set_text_fmt(label_speed_kmh_set, "%d.%d", speed_int, speed_frac);

    int climb_int = (int)roundf(g_treadmill_state.climb_percent);
    lv_label_set_text_fmt(label_climb_percent_set, "%d", climb_int);
}

void ui_init(void) {
    // Cargar el contador de cera desde NVS
    g_treadmill_state.total_running_seconds = load_wax_counter_from_nvs();
    g_treadmill_state.target_climb_percent = 0.0f; // Inicializar inclinación objetivo

    create_styles();
    create_training_select_screen();  // Crear pantalla de selección primero
    create_ble_scan_screen();          // Crear pantalla de escaneo BLE
    create_loading_screen();           // Crear pantalla de carga (descarga)
    create_uploading_screen();         // Crear pantalla de subida
    create_main_screen();
    create_set_screen();
    create_wax_screen();
    create_shutdown_screen();
    create_wifi_screens();
    lv_scr_load(scr_training_select);  // Mostrar pantalla de selección al inicio
}

//==================================================================================
// 8. PUBLIC UI FUNCTIONS
//==================================================================================

void ui_speed_inc(void) {
    bool should_beep = false;
    float new_target_speed;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_beep = true;

        // Check if a ramp is active (i.e., target speed is different from current real speed)
        // Use a small epsilon for float comparison
        if (fabsf(g_treadmill_state.target_speed - g_treadmill_state.speed_kmh) > 0.05f) { // Ramp is active
            new_target_speed = g_treadmill_state.speed_kmh; // Stop at current real speed
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL; // Ensure ramp is stopped
        } else { // No ramp active, or ramp has finished
            new_target_speed = g_treadmill_state.target_speed + 0.1f;
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL; // Ensure ramp is normal
        }

        if (new_target_speed > MAX_SPEED_KMH) new_target_speed = MAX_SPEED_KMH;
        g_treadmill_state.target_speed = new_target_speed;
    } else {
        new_target_speed = g_treadmill_state.target_speed; // Maintain the current target if stopped/cooldown
    }
    xSemaphoreGive(g_state_mutex);

    // Enviar comando al esclavo via RS485
    if (should_beep) {
        cm_master_set_speed(new_target_speed);
        audio_play_beep();
    }
}

void ui_speed_dec(void) {
    bool should_beep = false;
    float new_target_speed;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_beep = true;

        // Check if a ramp is active (i.e., target speed is different from current real speed)
        // Use a small epsilon for float comparison
        if (fabsf(g_treadmill_state.target_speed - g_treadmill_state.speed_kmh) > 0.05f) { // Ramp is active
            new_target_speed = g_treadmill_state.speed_kmh; // Stop at current real speed
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL; // Ensure ramp is stopped
        } else { // No ramp active, or ramp has finished
            new_target_speed = g_treadmill_state.target_speed - 0.1f;
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL; // Ensure ramp is normal
        }

        if (new_target_speed < 0.0f) new_target_speed = 0.0f;
        g_treadmill_state.target_speed = new_target_speed;
    } else {
        new_target_speed = g_treadmill_state.target_speed; // Maintain the current target if stopped/cooldown
    }
    xSemaphoreGive(g_state_mutex);

    // Enviar comando al esclavo via RS485
    if (should_beep) {
        cm_master_set_speed(new_target_speed);
        audio_play_beep();
    }
}

void ui_climb_inc(void) {
    bool should_beep = false;
    float new_target_climb;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_beep = true;

        // Incrementar objetivo en 1%
        new_target_climb = g_treadmill_state.target_climb_percent + 1.0f;

        if (new_target_climb > MAX_CLIMB_PERCENT) new_target_climb = MAX_CLIMB_PERCENT;
        g_treadmill_state.target_climb_percent = new_target_climb;
    } else {
        new_target_climb = g_treadmill_state.target_climb_percent; // Maintain the current target if stopped/cooldown
    }
    xSemaphoreGive(g_state_mutex);

    if (should_beep) {
        cm_master_set_incline(new_target_climb);
        audio_play_beep();
    }
}

void ui_climb_dec(void) {
    bool should_beep = false;
    float new_target_climb;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_beep = true;

        // Decrementar objetivo en 1%
        new_target_climb = g_treadmill_state.target_climb_percent - 1.0f;

        if (new_target_climb < 0.0f) new_target_climb = 0.0f;
        g_treadmill_state.target_climb_percent = new_target_climb;
    } else {
        new_target_climb = g_treadmill_state.target_climb_percent; // Maintain the current target if stopped/cooldown
    }
    xSemaphoreGive(g_state_mutex);

    if (should_beep) {
        cm_master_set_incline(new_target_climb);
        audio_play_beep();
    }
}

void ui_stop_resume(void) {
    audio_play_beep();

    // Variables locales para decidir qué hacer con la UI fuera del mutex
    bool was_stopped;
        // bool need_update_callbacks = false;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (g_treadmill_state.is_cooling_down) {
        ESP_LOGW(TAG, "ui_stop_resume: ignorado porque is_cooling_down=true");
        xSemaphoreGive(g_state_mutex);
        return;
    }

    was_stopped = g_treadmill_state.is_stopped;
    ESP_LOGI(TAG, "ui_stop_resume: was_stopped=%d", was_stopped);

    if (!g_treadmill_state.is_stopped) {
        g_treadmill_state.is_stopped = true;
        g_treadmill_state.is_resuming = false;
        g_treadmill_state.speed_before_stop = g_treadmill_state.target_speed;
        g_treadmill_state.target_speed = 0.0f;
        g_treadmill_state.ramp_mode = RAMP_MODE_STOP_STOP;
        ESP_LOGI(TAG, "ui_stop_resume: STOP activado");

        // Enviar comando de velocidad 0 al slave
        xSemaphoreGive(g_state_mutex);
        cm_master_set_speed(0.0f);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    } else {
        g_treadmill_state.is_stopped = false;
        g_treadmill_state.is_resuming = true;
        g_treadmill_state.resume_from_stop = false;
        float resume_speed = g_treadmill_state.speed_before_stop;
        g_treadmill_state.target_speed = resume_speed;
        g_treadmill_state.ramp_mode = RAMP_MODE_STOP_RESUME;
        ESP_LOGI(TAG, "ui_stop_resume: RESUME activado");

        // Enviar comando de velocidad de reanudación al slave
        xSemaphoreGive(g_state_mutex);
        cm_master_set_speed(resume_speed);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    }
    xSemaphoreGive(g_state_mutex);

    // Actualizar UI fuera del mutex
    // NO actualizar callbacks aquí - los botones físicos usan ui_back_to_training()
    // que comprueba buttons_are_stop_mode
    if (!was_stopped) {
        lv_label_set_text(label_stop_btn, "RESUME");
        // Cambiar el botón COOL DOWN a END (rojo)
        ESP_LOGI(TAG, "Cambiando botón COOL DOWN a END (rojo)");
        lv_label_set_text(label_cooldown_btn, "END");
        lv_obj_set_style_text_align(label_cooldown_btn, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_add_event_cb(btn_cooldown, end_event_cb, LV_EVENT_CLICKED, NULL);
        // Aplicar color rojo sólido brillante al botón END
        lv_obj_set_style_bg_color(btn_cooldown, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_cooldown, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        // Asegurar que el texto sea blanco y visible
        lv_obj_set_style_text_color(label_cooldown_btn, lv_color_hex(0xFFFFFF), 0);
        // Forzar actualización visual
        lv_obj_invalidate(btn_cooldown);
        lv_obj_invalidate(label_cooldown_btn);
        set_info_text_persistent("Pulsa RESUME para continuar con el ejercicio.");
    } else {
        lv_label_set_text(label_stop_btn, "STOP");
        // Restaurar el botón COOL DOWN
        ESP_LOGI(TAG, "Restaurando botón COOL DOWN");
        lv_label_set_text(label_cooldown_btn, "COOL\nDOWN");
        lv_obj_set_style_text_align(label_cooldown_btn, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_add_event_cb(btn_cooldown, cool_down_event_cb, LV_EVENT_CLICKED, NULL);
        // Restaurar color original del botón COOL DOWN (color predeterminado del tema)
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
        // Restaurar color de texto original
        lv_obj_remove_local_style_prop(label_cooldown_btn, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(label_cooldown_btn, LV_STYLE_TEXT_ALIGN, 0);
        // Forzar actualización visual
        lv_obj_invalidate(btn_cooldown);
        lv_obj_invalidate(label_cooldown_btn);
        lv_label_set_text(ta_info, "");
    }

    // Actualizar estado visual de botones +/-, SET
    update_button_states_visual();
}

void ui_cool_down(void) {
    audio_play_beep();

    // Variables locales para decidir qué hacer con la UI fuera del mutex
    bool was_cooling_down;
    // bool need_update_callbacks = false;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (g_treadmill_state.is_stopped) {
        xSemaphoreGive(g_state_mutex);
        return;
    }

    was_cooling_down = g_treadmill_state.is_cooling_down;

    if (!g_treadmill_state.is_cooling_down) {
        g_treadmill_state.is_cooling_down = true;
        g_treadmill_state.is_resuming = false;
        g_treadmill_state.speed_before_stop = g_treadmill_state.target_speed;
        g_treadmill_state.target_speed = 0.0f;

        float time_to_stop_s = g_treadmill_state.speed_before_stop / COOLDOWN_RAMP_RATE_KMH_S;
        float half_time_s = time_to_stop_s / 2.0f;

        if (half_time_s > 0.1f) {
            g_treadmill_state.cooldown_climb_ramp_rate = g_treadmill_state.climb_percent / half_time_s;
        } else {
            g_treadmill_state.climb_percent = 0.0f;
            g_treadmill_state.cooldown_climb_ramp_rate = 0.0f;
        }

        g_treadmill_state.ramp_mode = RAMP_MODE_COOLDOWN_STOP;
        // NO modificar buttons_are_stop_mode - se gestiona en ui_update_task

        // Nota: No enviamos cm_master_set_speed aquí porque la velocidad
        // se irá reduciendo gradualmente en ui_update_task
    } else {
        g_treadmill_state.is_cooling_down = false;
        g_treadmill_state.is_resuming = true;
        g_treadmill_state.resume_from_stop = false;
        float resume_speed = g_treadmill_state.speed_before_stop;
        g_treadmill_state.target_speed = resume_speed;
        g_treadmill_state.ramp_mode = RAMP_MODE_COOLDOWN_RESUME;

        // Enviar comando de velocidad de reanudación al slave
        xSemaphoreGive(g_state_mutex);
        cm_master_set_speed(resume_speed);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    }
    xSemaphoreGive(g_state_mutex);

    // Actualizar UI fuera del mutex
    if (!was_cooling_down) {
        lv_label_set_text(label_cooldown_btn, "RESUME");
        // Cambiar el botón STOP a END (rojo)
        ESP_LOGI(TAG, "Cambiando botón STOP a END (rojo)");
        lv_label_set_text(label_stop_btn, "END");
        lv_obj_set_style_text_align(label_stop_btn, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_event_cb(btn_stop, stop_resume_event_cb);
        lv_obj_add_event_cb(btn_stop, end_event_cb, LV_EVENT_CLICKED, NULL);
        // Aplicar color rojo sólido brillante al botón END
        lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_stop, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        // Asegurar que el texto sea blanco y visible
        lv_obj_set_style_text_color(label_stop_btn, lv_color_hex(0xFFFFFF), 0);
        // Forzar actualización visual
        lv_obj_invalidate(btn_stop);
        lv_obj_invalidate(label_stop_btn);

        set_info_text_persistent("Pulsa RESUME para continuar con el ejercicio.");
    } else {
        lv_label_set_text(label_cooldown_btn, "COOL\nDOWN");
        // Restaurar el botón STOP
        ESP_LOGI(TAG, "Restaurando botón STOP");
        lv_label_set_text(label_stop_btn, "STOP");
        lv_obj_set_style_text_align(label_stop_btn, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_event_cb(btn_stop, end_event_cb);
        lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);
        // Restaurar color original del botón STOP (color predeterminado del tema)
        lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
        // Restaurar color de texto original
        lv_obj_remove_local_style_prop(label_stop_btn, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(label_stop_btn, LV_STYLE_TEXT_ALIGN, 0);
        // Forzar actualización visual
        lv_obj_invalidate(btn_stop);
        lv_obj_invalidate(label_stop_btn);
        lv_label_set_text(ta_info, "");
    }

    // Actualizar estado visual de botones +/-, SET
    update_button_states_visual();
}

void ui_set_speed(void) {
    bool should_switch = false;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_switch = true;
        _switch_to_set_screen_internal(SET_MODE_SPEED);
    }
    xSemaphoreGive(g_state_mutex);

    if (should_switch) {
        audio_play_beep();
        lv_scr_load(scr_set);
    }
}

void ui_set_climb(void) {
    bool should_switch = false;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_switch = true;
        _switch_to_set_screen_internal(SET_MODE_CLIMB);
    }
    xSemaphoreGive(g_state_mutex);

    if (should_switch) {
        audio_play_beep();
        lv_scr_load(scr_set);
    }
}

static bool _handle_numpad_press_internal(char digit) {
    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT || g_treadmill_state.set_mode == SET_MODE_CLIMB) {
        // Para peso e inclinación solo aceptamos 2 dígitos
        if (g_treadmill_state.set_digit_index >= 2) return false;

        // Validar que no exceda el máximo
        char temp_buffer[3];
        strncpy(temp_buffer, g_treadmill_state.set_buffer, g_treadmill_state.set_digit_index);
        temp_buffer[g_treadmill_state.set_digit_index] = digit;
        temp_buffer[g_treadmill_state.set_digit_index + 1] = '\0';

        float proposed_value = atof(temp_buffer);
        float max_value = (g_treadmill_state.set_mode == SET_MODE_WEIGHT) ? 200.0f : MAX_CLIMB_PERCENT;

        if (proposed_value > max_value) {
            ESP_LOGI(TAG, "Dígito inválido '%c'. El valor propuesto %.0f excede el máximo %.0f", digit, proposed_value, max_value);
            return false;
        }

        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = digit;
        g_treadmill_state.set_digit_index++;
        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = '\0';

        _update_set_display_text_internal();

        return (g_treadmill_state.set_digit_index >= 2);  // Completado cuando tenemos 2 dígitos
    } else {
        // Comportamiento original para velocidad (3 dígitos con decimal)
        if (g_treadmill_state.set_digit_index >= 3) return false;

        char temp_buffer[4];
        strncpy(temp_buffer, g_treadmill_state.set_buffer, g_treadmill_state.set_digit_index);
        temp_buffer[g_treadmill_state.set_digit_index] = digit;
        temp_buffer[g_treadmill_state.set_digit_index + 1] = '\0';

        for (int i = strlen(temp_buffer); i < 3; ++i) {
            temp_buffer[i] = '0';
            temp_buffer[i+1] = '\0';
        }

        float proposed_value = atof(temp_buffer) / 10.0f;

        if (proposed_value > MAX_SPEED_KMH) {
            ESP_LOGI(TAG, "Dígito inválido '%c'. El valor propuesto %.1f excede el máximo %.1f", digit, proposed_value, MAX_SPEED_KMH);
            return false;
        }

        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = digit;
        g_treadmill_state.set_digit_index++;
        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = '\0';

        _update_set_display_text_internal();

        return (g_treadmill_state.set_digit_index >= 3);
    }
}

bool ui_handle_numpad_press(char digit) {
    audio_play_beep();
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool result = _handle_numpad_press_internal(digit);
    xSemaphoreGive(g_state_mutex);
    return result;
}

void ui_confirm_set_value(void) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);

    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT) {
        // Para peso, el valor es directo (sin división por 10)
        float weight = atof(g_treadmill_state.set_buffer);
        if (weight < 30.0f) weight = 30.0f;  // Mínimo 30 kg
        if (weight > 200.0f) weight = 200.0f; // Máximo 200 kg
        g_treadmill_state.user_weight_kg = weight;
        // NO marcar weight_entered = true todavía, esperamos a que la cinta empiece a moverse

        // Los botones WEIGHT y BACK se mantienen para permitir corrección
        // Solo se cambiarán cuando la cinta empiece a moverse

        char weight_msg[80];
        sprintf(weight_msg, "Peso: %dkg - Selecciona una velocidad para comenzar", (int)weight);
        set_info_text_persistent(weight_msg);

        // Marcar que estamos mostrando el peso en el campo de Kcal ANTES de cambiar de pantalla
        showing_weight_in_kcal_field = true;

        // Mostrar el peso en el label de Kcal (pantalla principal) y establecer la unidad "kg" ANTES de _switch_to_main_screen_internal
        bsp_display_lock(0);
        lv_label_set_text_fmt(label_kcal, "%d", (int)weight);
        lv_label_set_text(unit_kcal_main, "kg");  // Unidad en pantalla MAIN
        bsp_display_unlock();

        _switch_to_main_screen_internal();
    } else {
        bool is_speed_mode = (g_treadmill_state.set_mode == SET_MODE_SPEED);
        float final_value;

        if (is_speed_mode) {
            // Velocidad: 3 dígitos divididos por 10 (ej: "051" = 5.1 km/h)
            final_value = atof(g_treadmill_state.set_buffer) / 10.0f;
            if (final_value > MAX_SPEED_KMH) final_value = MAX_SPEED_KMH;
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
            g_treadmill_state.target_speed = final_value;
        } else { // SET_MODE_CLIMB
            // Inclinación: 2 dígitos sin dividir (ej: "05" = 5%)
            final_value = atof(g_treadmill_state.set_buffer);
            if (final_value > MAX_CLIMB_PERCENT) final_value = MAX_CLIMB_PERCENT;
            g_treadmill_state.target_climb_percent = final_value;
        }

        xSemaphoreGive(g_state_mutex);

        // Enviar comando al esclavo via RS485
        if (is_speed_mode) {
            cm_master_set_speed(final_value);
        } else {
            cm_master_set_incline(final_value);
        }

        // Actualizar inmediatamente los labels en la pantalla principal
        bsp_display_lock(0);
        if (is_speed_mode) {
            // Actualizar velocidad
            int speed_int = (int)final_value;
            int speed_frac = (int)((final_value - speed_int) * 10);
            lv_label_set_text_fmt(label_speed_kmh, "%d.%d", speed_int, speed_frac);

            // Actualizar pace (min:seg por km)
            if (final_value > 0.1f) {
                float pace_min_per_km = 60.0f / final_value;
                int pace_int = (int)pace_min_per_km;
                int pace_frac = (int)((pace_min_per_km - pace_int) * 60);
                lv_label_set_text_fmt(label_speed_pace, "%d:%02d", pace_int, pace_frac);
            } else {
                lv_label_set_text(label_speed_pace, "--:--");
            }
        } else {
            // Actualizar pendiente (sin decimales)
            int climb_int = (int)roundf(final_value);
            lv_label_set_text_fmt(label_climb_percent, "%d", climb_int);
        }
        bsp_display_unlock();

        _switch_to_main_screen_internal();
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    }
    lv_label_set_text(ta_info, "");
    lv_label_set_text(ta_info_set, "");
    lv_scr_load(scr_main);
    xSemaphoreGive(g_state_mutex);
}

void ui_switch_to_main_screen_from_timer(void) {
    _switch_to_main_screen_internal();
    lv_scr_load(scr_main);
}

bool ui_is_main_screen_active(void) {
    return lv_scr_act() == scr_main;
}

bool ui_is_training_select_screen_active(void) {
    return lv_scr_act() == scr_training_select;
}

void ui_loading_complete(void) {
    ESP_LOGI(TAG, "Loading complete - switching to main screen");

    if (g_download_mutex && xSemaphoreTake(g_download_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_downloaded_file_content != NULL && g_downloaded_file_size > 0) {
            // Find the first newline character
            char *first_line = g_downloaded_file_content;
            char *newline = strchr(first_line, '\n');
            if (newline != NULL) {
                // Null-terminate the string at the newline to get only the first line
                *newline = '\0';
            }

            // IMPORTANT: Make a copy before freeing the original buffer
            // lv_label_set_text() will create its own internal copy
            ESP_LOGI(TAG, "Displaying first line of downloaded file: %s", first_line);

            // Use lv_label_set_text which copies the string internally
            // This is safer than set_info_text_persistent with a pointer that will be freed
            lv_label_set_text(ta_info, first_line);
            if (text_area_timer) {
                lv_timer_del(text_area_timer);
                text_area_timer = NULL;
            }

            // Now it's safe to free the global buffer
            free(g_downloaded_file_content);
            g_downloaded_file_content = NULL;
            g_downloaded_file_size = 0;

        } else {
            lv_label_set_text(ta_info, "Error en la descarga del entreno.");
        }
        xSemaphoreGive(g_download_mutex);
    } else {
        lv_label_set_text(ta_info, "Error en la descarga del entreno.");
    }

    lv_scr_load(scr_main);
}

void ui_upload_complete(bool success) {
    ESP_LOGI(TAG, "Upload complete (success=%d)", success);

    if (success) {
        // Marcar que se ha subido exitosamente
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.has_uploaded = true;
        xSemaphoreGive(g_state_mutex);

        // Cargar la pantalla de apagado
        lv_scr_load(scr_shutdown);
    } else {
        // Si falla, volver a la pantalla principal y mostrar un error
        lv_scr_load(scr_main);
        set_info_text("Error al enviar el entrenamiento.");
    }
}

void ui_chest_toggle(void) {
    audio_play_beep();

    // Calcular siguiente estado (0->1->2->0)
    uint8_t next_state = (chest_value + 1) % 3;

    // Enviar comando al slave (0x02 = CHEST)
    cm_master_set_fan(0x02, next_state);

    // NO actualizar la UI inmediatamente, esperar respuesta del slave
    // La actualización se hará en ui_update_task() cuando llegue RSP_STATUS
}

void ui_head_toggle(void) {
    audio_play_beep();

    // Calcular siguiente estado (0->1->2->0)
    uint8_t next_state = (head_value + 1) % 3;

    // Enviar comando al slave (0x01 = HEAD)
    cm_master_set_fan(0x01, next_state);

    // NO actualizar la UI inmediatamente, esperar respuesta del slave
    // La actualización se hará en ui_update_task() cuando llegue RSP_STATUS
}

static void wax_event_cb(lv_event_t *e) {
    audio_play_beep();
    bsp_display_lock(0);

    // Actualizar el contador de horas en la pantalla
    uint32_t total_seconds = g_treadmill_state.total_running_seconds;
    uint32_t hours = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    lv_label_set_text_fmt(label_wax_hours, "%lu:%02lu", hours, minutes);

    lv_scr_load(scr_wax);
    bsp_display_unlock();
}

static void apply_wax_event_cb(lv_event_t *e) {
    audio_play_beep();

    // Activar la bomba de cera (ON)
    ESP_LOGI(TAG, "Applying wax...");
    cm_master_set_relay(0x01, 1);

    // Esperar un momento (simulación - en producción aquí esperarías el ACK)
    vTaskDelay(pdMS_TO_TICKS(2000));  // 2 segundos

    // Desactivar la bomba de cera (OFF)
    cm_master_set_relay(0x01, 0);
    ESP_LOGI(TAG, "Wax application complete");

    // Resetear el contador de horas
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.total_running_seconds = 0;
    xSemaphoreGive(g_state_mutex);

    // Guardar el contador reseteado en NVS
    save_wax_counter_to_nvs(0);

    // Actualizar el display
    lv_label_set_text(label_wax_hours, "0:00");
}

static void wax_back_event_cb(lv_event_t *e) {
    audio_play_beep();
    bsp_display_lock(0);
    lv_scr_load(scr_training_select);
    bsp_display_unlock();
}

void ui_weight_entry(void) {
    ESP_LOGI(TAG, "ui_weight_entry: buttons_are_stop_mode=%d", buttons_are_stop_mode);
    // Verificar si los botones están en modo STOP/COOL DOWN
    if (buttons_are_stop_mode) {
        ESP_LOGI(TAG, "ui_weight_entry: llamando ui_cool_down()");
        // Actuar como COOL DOWN (botón derecho)
        ui_cool_down();
    } else {
        // Actuar como WEIGHT: abrir entrada de peso
        audio_play_beep();
        bsp_display_lock(0);
        _switch_to_set_screen_internal(SET_MODE_WEIGHT);
        lv_scr_load(scr_set);
        bsp_display_unlock();
    }
}

void ui_back_to_training(void) {
    // Verificar si los botones están en modo STOP/COOL DOWN
    ESP_LOGI(TAG, "ui_back_to_training ENTRY: buttons_are_stop_mode=%d, tick=%lu",
             buttons_are_stop_mode, (unsigned long)xTaskGetTickCount());
    if (buttons_are_stop_mode) {
        // Actuar como STOP (botón izquierdo)
        ESP_LOGI(TAG, "ui_back_to_training: llamando ui_stop_resume()");
        ui_stop_resume();
        ESP_LOGI(TAG, "ui_back_to_training: ui_stop_resume() completado");
    } else {
        // Actuar como BACK: volver a selección de entrenamientos
        audio_play_beep();
        bsp_display_lock(0);
        if (wifi_check_timer) {
            lv_timer_del(wifi_check_timer);
            wifi_check_timer = NULL;
        }
        wifi_connected_timestamp = 0;
        lv_scr_load(scr_training_select);
        wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 100, NULL);
        bsp_display_unlock();
    }
}

void ui_select_training(int training_number) {
    audio_play_beep();

    // Guardar el tipo de entrenamiento seleccionado y resetear flags
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.selected_training = training_number;
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    g_treadmill_state.target_climb_percent = 0.0f; // Resetear inclinación objetivo
    xSemaphoreGive(g_state_mutex);

    // Enviar comando de reset de inclinación al esclavo
    cm_master_set_incline(0.0f);

    // Limpiar timer de WiFi
    bsp_display_lock(0);
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }
    bsp_display_unlock();

    switch(training_number) {
        case 1:
            ESP_LOGI(TAG, "Entrenamiento libre seleccionado (botón físico)");
            bsp_display_lock(0);
            lv_scr_load(scr_main);
            set_info_text_persistent("Selecciona una velocidad para comenzar");
            bsp_display_unlock();
            break;
        case 2:
            ESP_LOGI(TAG, "Entrenamiento Itsaso seleccionado (botón físico) - iniciando descarga");
            bsp_display_lock(0);
            lv_scr_load(scr_loading);  // Pantalla negra durante descarga
            bsp_display_unlock();
            wifi_download_plan("itsaso");
            break;
        case 3:
            ESP_LOGI(TAG, "Entrenamiento Ina seleccionado (botón físico) - iniciando descarga");
            bsp_display_lock(0);
            lv_scr_load(scr_loading);  // Pantalla negra durante descarga
            bsp_display_unlock();
            wifi_download_plan("ina");
            break;
        case 4:
            ESP_LOGI(TAG, "Entrenamiento Alain seleccionado (botón físico)");
            bsp_display_lock(0);
            lv_scr_load(scr_main);
            set_info_text_persistent("Los enanos tienen que usar esta cinta con supervision de aita o ama.");
            bsp_display_unlock();
            break;
        case 5:
            ESP_LOGI(TAG, "Entrenamiento Urko seleccionado (botón físico)");
            bsp_display_lock(0);
            lv_scr_load(scr_main);
            set_info_text_persistent("Los enanos tienen que usar esta cinta con supervision de aita o ama.");
            bsp_display_unlock();
            break;
        default:
            ESP_LOGW(TAG, "Número de entrenamiento inválido: %d", training_number);
            break;
    }
}
// WiFi selector callback - wrapper that calls ui_open_wifi_selector
static void wifi_selector_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Botón WiFi presionado - abriendo lista de redes");
    ui_open_wifi_list();
}