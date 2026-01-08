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
#include "ia_sync.h"
#include "ia_telemetry.h"
#include "freertos/task.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "imu_service.h"


LV_FONT_DECLARE(chivo_mono_70);
LV_FONT_DECLARE(chivo_mono_100);
extern const lv_font_t lv_font_montserrat_28;
extern const lv_font_t lv_font_montserrat_26;
extern const lv_font_t lv_font_montserrat_24;
extern const lv_font_t lv_font_montserrat_22;
extern const lv_font_t lv_font_montserrat_20;

const char *TAG = "UI";  // Accesible desde ui_wifi.c
// UI functions from ui_wifi.c are declared in ui.h




const float COOLDOWN_RAMP_RATE_KMH_S = 10.0f / 120.0f; // Rampa lenta de 2 minutos para el cool down
const float STOP_RAMP_RATE_KMH_S = 5.0f;    // Rampa rápida para detener/reanudar
const float COOLDOWN_RESUME_RAMP_RATE_KMH_S = 0.1f; // 0.1 km/h cada segundo para reanudar

static uint32_t last_speed_ramp_update_ms = 0;

//==================================================================================
// 1B. FUNCIONES DE PERSISTENCIA (NVS)
//==================================================================================

#define NVS_NAMESPACE_WAX "wax_maintenance"
#define NVS_KEY_TOTAL_SECONDS "total_seconds"


#define NVS_NAMESPACE_SETTINGS "app_settings"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_VOLUME "volume"
#define NVS_KEY_SENSITIVITY "sensitivity"


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

static void load_sensitivity_from_nvs(uint8_t *sens) {
    nvs_handle_t nvs_handle;
    *sens = 50; // Default
    if (nvs_open(NVS_NAMESPACE_SETTINGS, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, NVS_KEY_SENSITIVITY, sens);
        nvs_close(nvs_handle);
    }
}


static void save_app_setting_to_nvs(const char *key, uint8_t value) {
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE_SETTINGS, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, key, value);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
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
static lv_obj_t *label_stride;
static lv_obj_t *label_stride;
static lv_obj_t *label_dist_set;
static lv_obj_t *unit_kcal_main;  // Label de unidad "Kcal" en pantalla MAIN
static lv_obj_t *unit_kcal_set;  // Label de unidad "Kcal" en pantalla SET
static lv_obj_t *label_stop_btn;
static lv_obj_t *label_cooldown_btn;
static lv_obj_t *btn_stop;
static lv_obj_t *btn_cooldown;
static lv_obj_t *btn_upload_training;
static lv_obj_t *btn_test_upload;
static lv_obj_t *ta_info;
static lv_obj_t *label_status_wifi;
static lv_obj_t *label_status_ble;

LV_IMG_DECLARE(icon_main);

static ia_plan_t g_current_plan;
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

// Variables para lógica inteligente de entrada de velocidad
static bool waiting_for_second_digit = false;
static lv_timer_t *speed_input_timeout_timer = NULL;
static char first_speed_digit = '\0';
static bool confirming_in_progress = false;

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
static lv_obj_t *label_stride_set;
static lv_obj_t *label_1000m; // Reference to update distance limit label

// --- Dynamic Power Profile Variables ---
#define POWER_HISTORY_SIZE 100
static float power_history[POWER_HISTORY_SIZE];
static lv_obj_t *power_dots[POWER_HISTORY_SIZE];
static lv_point_t power_points[POWER_HISTORY_SIZE];
static lv_obj_t *power_line;
static float max_power_ref = 300.0f;
static float dist_resolution_m = 10.0f;
static int current_power_index = 0;
static double last_dist_ref_km = 0;
static float power_accumulator = 0;
static int power_sample_count = 0;
static lv_obj_t *ta_info_set;

// -- Pantalla de Ajustes APP --
static lv_obj_t *scr_app_settings;
static lv_obj_t *arc_brightness;
static lv_obj_t *arc_volume;
static lv_obj_t *label_brightness_pct;
static lv_obj_t *label_volume_pct;
static lv_obj_t *label_sensitivity_pct;


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
    lv_obj_t *stride_label;
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
static void create_app_settings_screen(void);
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
static void stop_from_cooldown_event_cb(lv_event_t *e);
static void wifi_selector_event_cb(lv_event_t *e);
static void wax_event_cb(lv_event_t *e);
static void app_settings_event_cb(lv_event_t *e);
static void brightness_arc_event_cb(lv_event_t *e);
static void volume_arc_event_cb(lv_event_t *e);
static void app_settings_back_event_cb(lv_event_t *e);
static void manual_up_event_cb(lv_event_t *e);
static void manual_down_event_cb(lv_event_t *e);
static void apply_wax_event_cb(lv_event_t *e);
static void wax_back_event_cb(lv_event_t *e);
static void on_report_sent(bool success, const char *error_msg);
static void ui_stop_from_cooldown(void);

// --- IA Sync Callbacks ---
static void on_plan_received(const ia_plan_t *plan, const char *error_msg) {
    bsp_display_lock(0);
    if (plan) {
        ESP_LOGI(TAG, "Plan recibido: %s con %d bloques", plan->plan_id, plan->block_count);
        memcpy(&g_current_plan, plan, sizeof(ia_plan_t));
        
        // Initialize execution state (reset even if we just show JSON)
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_current_plan = *plan;
        g_treadmill_state.plan_running = false; // Don't start automatically yet per user request
        g_treadmill_state.current_block_idx = 0;
        g_treadmill_state.block_elapsed_seconds = 0;
        g_treadmill_state.block_distance_km = 0;
        g_treadmill_state.block_kcal = 0;
        xSemaphoreGive(g_state_mutex);

        lv_scr_load(scr_main);
        
        // Mostrar Tramo y Bloque del primer bloque en el recuadro (ta_info)
        if (plan->block_count > 0) {
            char info_buf[128];
            snprintf(info_buf, sizeof(info_buf), "%s\n%s", 
                     plan->blocks[0].tramo_label, 
                     plan->blocks[0].bloque_label);
            lv_label_set_text(ta_info, info_buf);
        } else {
            lv_label_set_text(ta_info, "Plan sin bloques");
        }
        
        cm_master_set_training_mode(true);
    } else {
        ESP_LOGE(TAG, "Error al recibir plan: %s", error_msg ? error_msg : "Desocnocido");
        set_info_text_persistent("Error al descargar plan. Intentalo de nuevo.");
        lv_scr_load(scr_training_select);
    }
    bsp_display_unlock();
}

static void test_upload_event_cb(lv_event_t *e) {
    audio_play_beep();
    // Ejemplo de telemetría comprimida para prueba (formato exacto del txt)
    const char *test_data = "v,i,p,c,z; 10.0,0.0,120,160,1.10 | 10.5,0.5,122,161,1.11 | 11.2,1.0,125,165,1.15";
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    int training = g_treadmill_state.selected_training;
    xSemaphoreGive(g_state_mutex);

    // Si no hay entrenamiento seleccionado (estamos en test), usamos "Ina" por defecto
    const char *user = (training == 2) ? "Itsaso" : "Ina";
    const char *p_id = (g_current_plan.block_count > 0) ? g_current_plan.plan_id : "DEBUG-TEST";

    set_info_text_persistent("Subiendo reporte de prueba a Sheets...");
    ia_sync_upload_report(user, p_id, test_data, on_report_sent);
}

static void on_report_sent(bool success, const char *error_msg) {
    bsp_display_lock(0);
    if (success) {
        ESP_LOGI(TAG, "Reporte enviado con éxito");
        set_info_text_persistent("¡Entrenamiento guardado! Buen trabajo.");
        
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.has_uploaded = true;
        xSemaphoreGive(g_state_mutex);
        
        lv_scr_load(scr_training_select);
    } else {
        ESP_LOGE(TAG, "Error al enviar reporte: %s", error_msg ? error_msg : "Desconcido");
        set_info_text_persistent("Error al subir el entrenamiento.");
        lv_scr_load(scr_training_select);
    }
    bsp_display_unlock();
}


//==================================================================================
// 4. TAREA PRINCIPAL DE ACTUALIZACIÓN
//==================================================================================
static void text_area_clear_timer_cb(lv_timer_t *timer) {
    lv_label_set_text(ta_info, "");
    text_area_timer = NULL; // The timer is deleted automatically, just clear the handle.
}

static uint32_t wifi_connected_timestamp = 0;

static void wifi_check_timer_cb(lv_timer_t *timer) {
    bool wifi_connected = is_wifi_connected();
    bool internet_connected = is_internet_connected();

    // 1. Actualizar botones de entrenamiento según Internet
    if (internet_connected) {
        if (btn_training_itsaso && label_training_itsaso) {
            lv_obj_add_flag(btn_training_itsaso, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_color(label_training_itsaso, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(label_training_itsaso, "Entrenamiento Itsaso");
        }
        if (btn_training_ina && label_training_ina) {
            lv_obj_add_flag(btn_training_ina, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_color(label_training_ina, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(label_training_ina, "Entrenamiento Ina");
        }
    } else {
        if (btn_training_itsaso && label_training_itsaso) {
            lv_obj_clear_flag(btn_training_itsaso, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_color(label_training_itsaso, lv_color_hex(0x000000), 0);
            lv_label_set_text(label_training_itsaso, "Conectando...");
        }
        if (btn_training_ina && label_training_ina) {
            lv_obj_clear_flag(btn_training_ina, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_color(label_training_ina, lv_color_hex(0x000000), 0);
            lv_label_set_text(label_training_ina, "Conectando...");
        }
    }

    // 2. Actualizar Etiqueta WiFi (Abajo)
    if (label_status_wifi) {
        if (!wifi_connected) {
            lv_obj_set_style_text_color(label_status_wifi, lv_color_hex(0xFFFFFF), 0); // Blanco
            lv_label_set_text(label_status_wifi, "WiFi: No conectado");
        } else {
            char ssid[32];
            // Skip wifi_manager_get_current_ssid() if scanning to avoid timeout
            if (ui_wifi_is_scanning()) {
                strcpy(ssid, "Scanning...");
            } else {
                if (wifi_manager_get_current_ssid(ssid) != ESP_OK) strcpy(ssid, "Desconocido");
            }
            
            if (internet_connected) {
                lv_obj_set_style_text_color(label_status_wifi, lv_color_hex(0x00FF00), 0); // Verde
            } else {
                lv_obj_set_style_text_color(label_status_wifi, lv_color_hex(0xFFFF00), 0); // Amarillo
            }
            lv_label_set_text_fmt(label_status_wifi, "WiFi: %s", ssid);
        }
    }

    // 3. Actualizar Etiqueta Cardio BLE (Abajo)
    if (label_status_ble) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        bool ble_conn = g_treadmill_state.ble_connected;
        uint16_t pulse = g_treadmill_state.real_pulse;
        xSemaphoreGive(g_state_mutex);

        if (!ble_conn) {
            lv_obj_set_style_text_color(label_status_ble, lv_color_hex(0xFFFFFF), 0); // Blanco
            lv_label_set_text(label_status_ble, "Cardio BLE: No conectado");
        } else {
            lv_obj_set_style_text_color(label_status_ble, lv_color_hex(0x00FF00), 0); // Verde
            lv_label_set_text_fmt(label_status_ble, "Cardio BLE: %u BPM", pulse);
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
    // Poll IMU immediately at start
    imu_service_init(); 

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
    static int prev_steps = -1;
    static int prev_kcal = -1;
    static bool need_restore_weight_buttons = false;




    while (1) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        
        // Update IMU data in global state
        g_treadmill_state.steps = imu_service_get_steps();
        g_treadmill_state.cadence = imu_service_get_cadence();



        // --- Actualizar velocidad e inclinación reales desde el esclavo ---
        // Los valores reales vienen del esclavo, no se simulan localmente
        float real_speed_from_slave = cm_master_get_real_speed();
        float real_incline_from_slave = cm_master_get_current_incline();
        g_treadmill_state.speed_kmh = real_speed_from_slave;

        // Actualizar inclinación real desde el esclavo
        g_treadmill_state.climb_percent = real_incline_from_slave;

#ifdef SIMULATOR
        static bool demo_reset_done = false;
        // Simulador Demo: En el emulador sobreescribimos los valores leídos arriba (que serán 0)
        // para visualizar el gráfico sin necesidad de hardware real.
        if (lv_scr_act() == scr_main) {
            if (!demo_reset_done) {
                printf("DEMO: Resetting Power Profile Data\n");
                g_treadmill_state.total_distance_km = 0;
                g_treadmill_state.elapsed_seconds = 0;
                last_dist_ref_km = 0;
                current_power_index = 0;
                dist_resolution_m = 10.0f;
                max_power_ref = 300.0f;
                power_accumulator = 0;
                power_sample_count = 0;
                bsp_display_lock(0);
                for (int j = 0; j < 100; j++) {
                    if (power_dots[j]) {
                         lv_obj_add_flag(power_dots[j], LV_OBJ_FLAG_HIDDEN);
                    }
                    power_points[j].x = (lv_coord_t)(j * 9 + 4);
                    power_points[j].y = 0;
                }
                if (power_line) lv_obj_add_flag(power_line, LV_OBJ_FLAG_HIDDEN);
                if (label_1000m) lv_label_set_text(label_1000m, "1.000 m");
                bsp_display_unlock();
                demo_reset_done = true;
            }
            g_treadmill_state.total_distance_km += 0.020; // +20m cada 100ms
            g_treadmill_state.elapsed_seconds += 1;
            g_treadmill_state.speed_kmh = 12.0f + 6.0f * sinf(g_treadmill_state.elapsed_seconds * 0.1f);
            g_treadmill_state.climb_percent = 3.0f + 3.0f * cosf(g_treadmill_state.elapsed_seconds * 0.05f);
            g_treadmill_state.weight_entered = true; 
            if (g_treadmill_state.user_weight_kg < 1.0f) g_treadmill_state.user_weight_kg = 75.0f;
        } else {
            demo_reset_done = false;
        }
#endif

        // --- PROTECCIÓN SYNC MANUAL ---
        // Si hay un fallo detectado o estamos en la pantalla de ajustes,
        // sincronizamos el target con la posición real. Esto evita que los
        // frames SYNC periódicos del maestro obliguen a la base a bajar a 0%.
        bool incline_sensor_fault_active = cm_master_get_incline_sensor_fault();
        if (incline_sensor_fault_active || lv_scr_act() == scr_app_settings) {
            cm_master_set_incline(real_incline_from_slave);
            g_treadmill_state.target_climb_percent = real_incline_from_slave;
        }

        // --- Actualizar estados de ventiladores desde el esclavo ---
        uint8_t head_fan_from_slave = cm_master_get_head_fan_state();
        uint8_t chest_fan_from_slave = cm_master_get_chest_fan_state();

        // Actualizar valores locales
        head_value = head_fan_from_slave;
        chest_value = chest_fan_from_slave;

        // --- PROTECCIÓN CRÍTICA: Verificar fallo del sensor de fin de carrera ---
        static bool system_locked_due_to_sensor_fault = false;
        bool incline_sensor_fault = cm_master_get_incline_sensor_fault();

        if (incline_sensor_fault && !system_locked_due_to_sensor_fault) {
            system_locked_due_to_sensor_fault = true;

            // Detener la cinta inmediatamente
            g_treadmill_state.target_speed = 0.0f;
            cm_master_set_speed(0.0f);

            // Mostrar mensaje de error crítico
            bsp_display_lock(0);
            set_info_text_persistent("ERROR CRITICO: Sensor de inclinacion averiado. Contacte servicio tecnico.");

            // Desactivar todos los botones de control
            lv_obj_add_state(btn_speed_inc, LV_STATE_DISABLED);
            lv_obj_add_state(btn_speed_dec, LV_STATE_DISABLED);
            lv_obj_add_state(btn_speed_set, LV_STATE_DISABLED);
            lv_obj_add_state(btn_climb_inc, LV_STATE_DISABLED);
            lv_obj_add_state(btn_climb_dec, LV_STATE_DISABLED);
            lv_obj_add_state(btn_climb_set, LV_STATE_DISABLED);
            lv_obj_add_state(btn_stop, LV_STATE_DISABLED);
            lv_obj_add_state(btn_cooldown, LV_STATE_DISABLED);

            bsp_display_unlock();

            ESP_LOGE("UI", "═══════════════════════════════════════════════════");
            ESP_LOGE("UI", "  SISTEMA BLOQUEADO POR ERROR CRÍTICO");
            ESP_LOGE("UI", "  Sensor de fin de carrera de inclinación falló");
            ESP_LOGE("UI", "  Todos los controles desactivados");
            ESP_LOGE("UI", "═══════════════════════════════════════════════════");
        }

        // --- RECUPERACIÓN AUTOMÁTICA ---
        if (system_locked_due_to_sensor_fault && !incline_sensor_fault) {
            system_locked_due_to_sensor_fault = false;
            
            // Re-activar los botones
            bsp_display_lock(0);
            lv_obj_clear_state(btn_speed_inc, LV_STATE_DISABLED);
            lv_obj_clear_state(btn_speed_dec, LV_STATE_DISABLED);
            lv_obj_clear_state(btn_speed_set, LV_STATE_DISABLED);
            lv_obj_clear_state(btn_climb_inc, LV_STATE_DISABLED);
            lv_obj_clear_state(btn_climb_dec, LV_STATE_DISABLED);
            lv_obj_clear_state(btn_climb_set, LV_STATE_DISABLED);
            lv_obj_clear_state(btn_stop, LV_STATE_DISABLED);
            lv_obj_clear_state(btn_cooldown, LV_STATE_DISABLED);
            
            set_info_text("Sistema recuperado. Error de sensor corregido.");
            bsp_display_unlock();
            
            ESP_LOGI("UI", "Sistema DESBLOQUEADO - El sensor de fin de carrera volvió a responder");
        }

        // Si el sistema está bloqueado, forzar velocidad a 0 siempre
        if (system_locked_due_to_sensor_fault) {
            g_treadmill_state.target_speed = 0.0f;
            g_treadmill_state.speed_kmh = 0.0f;
            xSemaphoreGive(g_state_mutex);
            vTaskDelay(pdMS_TO_TICKS(UI_UPDATE_INTERVAL_MS));
            continue;  // Saltar el resto de la lógica de actualización
        }

        // Detectar si hemos parado completamente
        if (g_treadmill_state.speed_kmh < 0.05f && (g_treadmill_state.is_cooling_down || g_treadmill_state.is_stopped)) {
             // Podríamos forzar el fin del entrenamiento aquí, pero el usuario no lo ha pedido explícitamente ("END no salga")
             // Por ahora simplemente se queda a 0.
        }

        // --- Lógica de Rampa de Velocidad (Cool Down y Resume) ---
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (g_treadmill_state.ramp_mode == RAMP_MODE_COOLDOWN_STOP) {
            // Deceleración escalonada del Cool Down (Pasos de 0.5 km/h)
            uint32_t interval_ms = 30000; // Nivel 1 (30s)
            if (g_treadmill_state.cooldown_level == 2) interval_ms = 15000;
            else if (g_treadmill_state.cooldown_level == 3) interval_ms = 5000;

            if (now_ms - last_speed_ramp_update_ms >= interval_ms) {
                last_speed_ramp_update_ms = now_ms;
                float current_speed = g_treadmill_state.speed_kmh;
                if (current_speed > 0.5f) {
                    float new_target = current_speed - 0.5f;
                    // Redondear a .0 o .5 para consistencia
                    new_target = roundf(new_target * 2.0f) / 2.0f;
                    cm_master_set_speed(new_target);
                } else if (current_speed > 0) {
                    cm_master_set_speed(0.0f);
                }
            }
        } 
        else if (g_treadmill_state.is_resuming) {
            // Aceleración escalonada del Resume (0.5 km/h cada 1 segundo)
            if (now_ms - last_speed_ramp_update_ms >= 1000) {
                last_speed_ramp_update_ms = now_ms;
                float current_speed = g_treadmill_state.speed_kmh;
                float final_target = g_treadmill_state.target_speed;
                
                if (current_speed < final_target - 0.1f) {
                    float new_target = current_speed + 0.5f;
                    if (new_target > final_target) new_target = final_target;
                    // Redondear a .0 o .5 para consistencia
                    new_target = roundf(new_target * 2.0f) / 2.0f;
                    cm_master_set_speed(new_target);
                } else {
                    // Objetivo alcanzado
                    g_treadmill_state.is_resuming = false;
                    g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
                    cm_master_set_speed(final_target);
                }
            }
        }

        // --- Movimiento de Inclinación (Cool Down) ---
        // El objetivo de inclinación pasa directamente a 0% en ui_cool_down()
        // y el actuador lineal físico baja hasta el fin de carrera.

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

                // --- Lógica de Ejecución de Plan IA ---
                if (g_treadmill_state.plan_running && g_current_plan.block_count > 0) {
                    g_treadmill_state.block_elapsed_seconds++;
                    int idx = g_treadmill_state.current_block_idx;
                    ia_block_t *curr = &g_current_plan.blocks[idx];
                    
                    bool end_condition_met = false;
                    switch(curr->primary_cond_type) {
                        case IA_CONDITION_TIME:
                            if (g_treadmill_state.block_elapsed_seconds >= (uint32_t)curr->primary_cond_value) end_condition_met = true;
                            break;
                        case IA_CONDITION_DISTANCE:
                            if (g_treadmill_state.block_distance_km >= (double)curr->primary_cond_value) end_condition_met = true;
                            break;
                        case IA_CONDITION_KCAL:
                            if (g_treadmill_state.block_kcal >= curr->primary_cond_value) end_condition_met = true;
                            break;
                        case IA_CONDITION_BPM:
                            if (g_treadmill_state.ble_connected && g_treadmill_state.real_pulse >= (uint16_t)curr->primary_cond_value) end_condition_met = true;
                            break;
                    }

                    // Condición secundaria (seguridad por tiempo)
                    if (!end_condition_met && curr->secondary_cond_s > 0) {
                        if (g_treadmill_state.block_elapsed_seconds >= curr->secondary_cond_s) {
                            end_condition_met = true;
                            ESP_LOGW("UI", "IA_PLAN: Block transition triggered by safety secondary condition (time)");
                        }
                    }

                    if (end_condition_met) {
                        // Fin de bloque, pasar al siguiente
                        g_treadmill_state.current_block_idx++;
                        g_treadmill_state.block_elapsed_seconds = 0;
                        g_treadmill_state.block_distance_km = 0;
                        g_treadmill_state.block_kcal = 0;
                        
                        if (g_treadmill_state.current_block_idx < g_current_plan.block_count) {
                            int new_idx = g_treadmill_state.current_block_idx;
                            g_treadmill_state.target_speed = g_current_plan.blocks[new_idx].target_speed;
                            g_treadmill_state.target_climb_percent = g_current_plan.blocks[new_idx].target_incline;
                            
                            cm_master_set_speed(g_treadmill_state.target_speed);
                            cm_master_set_incline(g_treadmill_state.target_climb_percent);
                            
                            bsp_display_lock(0);
                            char info_buf[128];
                            snprintf(info_buf, sizeof(info_buf), "%s\n%s", 
                                     g_current_plan.blocks[new_idx].tramo_label, 
                                     g_current_plan.blocks[new_idx].bloque_label);
                            set_info_text_persistent(info_buf);
                            bsp_display_unlock();
                            
                            ESP_LOGI("UI", "IA_PLAN: Next block %d (%s). Targets: %.1f km/h, %.1f %%, BPM: %.1f", 
                                     new_idx, info_buf, g_treadmill_state.target_speed, 
                                     g_treadmill_state.target_climb_percent, g_current_plan.blocks[new_idx].target_bpm);
                        } else {
                            // Fin del plan
                            g_treadmill_state.plan_running = false;
                            bsp_display_lock(0);
                            set_info_text_persistent("¡Entrenamiento finalizado! Puedes pulsar STOP.");
                            bsp_display_unlock();
                            ESP_LOGI("UI", "IA_PLAN: Completed");
                        }
                    }
                }
            }
            // Check if minimum running time has been reached (10 seconds)
            if (!g_treadmill_state.has_run_minimum_time && g_treadmill_state.elapsed_seconds >= 10) {
                g_treadmill_state.has_run_minimum_time = true;
            }
            double distance_this_interval = (double)g_treadmill_state.speed_kmh / 3600.0 * (UI_UPDATE_INTERVAL_MS / 1000.0);
            g_treadmill_state.total_distance_km += distance_this_interval;
            g_treadmill_state.block_distance_km += distance_this_interval;

            // Calcular calorías usando la fórmula ACSM (solo si se ha introducido el peso)
            // kcal = [ (0.2 × velocidad (m/min) + 0.9 × velocidad (m/min) × pendiente (decimal) + 3.5) × peso (kg) × tiempo (min) ] ÷ 200
            if (g_treadmill_state.weight_entered) {
                float speed_m_min = g_treadmill_state.speed_kmh * 1000.0f / 60.0f;  // Convertir km/h a m/min
                float slope_decimal = g_treadmill_state.climb_percent / 100.0f;      // Convertir % a decimal
                float time_min = (UI_UPDATE_INTERVAL_MS / 1000.0f) / 60.0f;         // Tiempo en minutos para este intervalo

                float kcal_this_interval = ((0.2f * speed_m_min + 0.9f * speed_m_min * slope_decimal + 3.5f)
                                            * g_treadmill_state.user_weight_kg * time_min) / 200.0f;
                g_treadmill_state.sim_kcal += kcal_this_interval;
                g_treadmill_state.block_kcal += kcal_this_interval;
            }
            // Si no se ha introducido el peso, las kcal permanecen en 0

            // --- CÁLCULO DE POTENCIA DINÁMICA (WATTS) ---
            float weight_kg = g_treadmill_state.weight_entered ? g_treadmill_state.user_weight_kg : 75.0f;
            float speed_m_min_p = g_treadmill_state.speed_kmh * 1000.0f / 60.0f;
            float slope_decimal_p = g_treadmill_state.climb_percent / 100.0f;
            
            // VO2 = 0.2 * v + 0.9 * v * s + 3.5
            float vo2 = (0.2f * speed_m_min_p) + (0.9f * speed_m_min_p * slope_decimal_p) + 3.5f;
            // Watts = (VO2 * weight / 1000 * 5 kcal/L) * 69.73 W/kcal-min * 0.25 efficiency
            float current_power_watts = (vo2 * weight_kg / 1000.0f * 5.0f) * 69.73f * 0.25f;

            // Acumular para promediar el tramo
            power_accumulator += current_power_watts;
            power_sample_count++;

            // Verificar si hemos avanzado la distancia suficiente para el siguiente punto (segmento)
            double dist_delta_km = g_treadmill_state.total_distance_km - last_dist_ref_km;
            if (dist_delta_km >= (dist_resolution_m / 1000.0)) {
                float avg_power = power_accumulator / (float)power_sample_count;
                
                bsp_display_lock(0);
                // Auto-escala vertical si superamos la referencia
                if (avg_power > max_power_ref) {
                    max_power_ref = (float)((int)(avg_power / 50.0f) + 1) * 50.0f; // Redondear al alza de 50 en 50
                    // Re-escalar todos los puntos existentes
                    for (int j = 0; j < current_power_index; j++) {
                        int py_scaled = 190 - (int)(power_history[j] / max_power_ref * 170.0f) - 10;
                        if (py_scaled < 10) py_scaled = 10; 
                        if (py_scaled > 180) py_scaled = 180;
                        lv_obj_set_y(power_dots[j], py_scaled);
                        power_points[j].x = (lv_coord_t)(j * 9 + 4);
                        power_points[j].y = (lv_coord_t)(py_scaled + 2);
                    }
                    if (current_power_index > 1) lv_line_set_points(power_line, power_points, (uint16_t)current_power_index);
                }

                if (current_power_index < POWER_HISTORY_SIZE) {
                    power_history[current_power_index] = avg_power;
                    int py_new = 190 - (int)(avg_power / max_power_ref * 170.0f) - 10;
                    if (py_new < 10) py_new = 10;
                    if (py_new > 180) py_new = 180;
                    
                    lv_obj_set_y(power_dots[current_power_index], py_new);
                    lv_obj_clear_flag(power_dots[current_power_index], LV_OBJ_FLAG_HIDDEN);
                    
                    power_points[current_power_index].x = (lv_coord_t)(current_power_index * 9 + 4);
                    power_points[current_power_index].y = (lv_coord_t)(py_new + 2);
                    current_power_index++;
                    
                    if (current_power_index > 1) {
                        lv_line_set_points(power_line, power_points, (uint16_t)current_power_index);
                        lv_obj_clear_flag(power_line, LV_OBJ_FLAG_HIDDEN);
                    }
                } else {
                    // COMPRESIÓN: Al llegar al final del buffer (100 puntos)
                    dist_resolution_m *= 2.0f;
                    // Actualizar etiqueta de distancia final
                    lv_label_set_text_fmt(label_1000m, "%d.000 m", (int)(dist_resolution_m * 100 / 1000));
                    
                    // Comprimir el histórico (promediar de 2 en 2)
                    for (int j = 0; j < 50; j++) {
                        power_history[j] = (power_history[2 * j] + power_history[2 * j + 1]) / 2.0f;
                        int py_comp = 190 - (int)(power_history[j] / max_power_ref * 170.0f) - 10;
                        if (py_comp < 10) py_comp = 10;
                        if (py_comp > 180) py_comp = 180;
                        
                        lv_obj_set_y(power_dots[j], py_comp);
                        power_points[j].x = (lv_coord_t)(j * 9 + 4);
                        power_points[j].y = (lv_coord_t)(py_comp + 2);
                    }
                    // Ocultar resto de puntos
                    for (int j = 50; j < POWER_HISTORY_SIZE; j++) {
                        lv_obj_add_flag(power_dots[j], LV_OBJ_FLAG_HIDDEN);
                    }
                    current_power_index = 50;
                    lv_line_set_points(power_line, power_points, (uint16_t)current_power_index);
                }
                bsp_display_unlock();

                // Resetear acumulador para el siguiente tramo
                power_accumulator = 0;
                power_sample_count = 0;
                last_dist_ref_km = g_treadmill_state.total_distance_km;
            }
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
        set_mode_t set_mode_copy = g_treadmill_state.set_mode;
        int current_kcal = (int)(g_treadmill_state.sim_kcal + 0.5f);
        uint8_t head_fan_copy = head_value;
        uint8_t chest_fan_copy = chest_value;
        uint32_t current_steps_copy = g_treadmill_state.steps;


        xSemaphoreGive(g_state_mutex);
        bsp_display_lock(0);

        // --- Actualizaciones de UI con variables locales (fuera de la sección crítica) ---

        // Time
        if (elapsed_seconds_copy != prev_elapsed_seconds) {
            uint32_t hours = elapsed_seconds_copy / 3600;
            uint32_t minutes = (elapsed_seconds_copy % 3600) / 60;
            uint32_t seconds = elapsed_seconds_copy % 60;
            lv_label_set_text_fmt(label_time, "%"PRIu32":%02"PRIu32":%02"PRIu32, hours, minutes, seconds);
            lv_label_set_text_fmt(label_time_set, "%"PRIu32":%02"PRIu32":%02"PRIu32, hours, minutes, seconds);
            
            // Update Stride/Cadence labels
            lv_label_set_text_fmt(label_stride, "%.2f m", g_treadmill_state.cadence > 20.0f ? (g_treadmill_state.speed_kmh / g_treadmill_state.cadence) * 16.666f : 0.0f);
            // Optionally repurpose the secondary stride label or a new one for steps
            // For now, let's use the info area or a dedicated label if found
            lv_label_set_text_fmt(label_stride_set, "%lu", (unsigned long)g_treadmill_state.stride);

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
            prev_dist_value = dist_value;
            prev_dist_is_meters = is_meters;
        }

        // Speed
        float speed_to_display;
        uint32_t now_ms_ui = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        bool show_target = g_treadmill_state.is_adjusting_speed || 
                           (now_ms_ui - g_treadmill_state.speed_adjustment_end_ms < 1000);

        if (show_target) {
            speed_to_display = g_treadmill_state.target_speed;
        } else {
            speed_to_display = speed_kmh_copy;
        }

        int total_speed_tenths = (int)roundf(speed_to_display * 10.0f);
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

        // Climb percent (Display TARGET value, but BLINK if not reached)
        static uint32_t blink_counter = 0;
        blink_counter += UI_UPDATE_INTERVAL_MS;
        bool is_incline_moving = (fabsf(g_treadmill_state.target_climb_percent - climb_percent_copy) > 0.3f);
        bool blink_visible = (blink_counter % 1000 < 500); // 500ms on, 500ms off

        int climb_display_int = (int)roundf(g_treadmill_state.target_climb_percent);
        
        if (climb_display_int != prev_climb_int || is_incline_moving) {
            if (is_incline_moving && !blink_visible) {
                lv_label_set_text(label_climb_percent, "");
                if (set_mode_copy != SET_MODE_CLIMB && set_mode_copy != SET_MODE_WEIGHT) {
                    lv_label_set_text(label_climb_percent_set, "");
                }
            } else {
                lv_label_set_text_fmt(label_climb_percent, "%d", climb_display_int);
                if (set_mode_copy != SET_MODE_CLIMB && set_mode_copy != SET_MODE_WEIGHT) {
                    lv_label_set_text_fmt(label_climb_percent_set, "%d", climb_display_int);
                }
            }
            prev_climb_int = climb_display_int;
        }

        // Pace (M:SS)
        int pace_m = -1, pace_s = -1;
        bool pace_visible = false;
        if (speed_kmh_copy > 6.01f) { // Límite: 6.0 km/h -> 10:00 pace. Solo mostramos hasta 9:59.
            float pace_min_km = 60.0f / speed_kmh_copy;
            pace_m = (int)pace_min_km;
            pace_s = (int)((pace_min_km - (float)pace_m) * 60.0f + 0.5f);
            if (pace_s >= 60) {
                pace_s = 0;
                pace_m++;
            }
            if (pace_m < 10) pace_visible = true;
        }

        if (pace_m != prev_pace_int || pace_s != prev_pace_frac) {
            if (pace_visible) {
                lv_label_set_text_fmt(label_speed_pace, "%d:%02d", pace_m, pace_s);
                if (set_mode_copy != SET_MODE_SPEED) {
                    lv_label_set_text_fmt(label_speed_pace_set, "%d:%02d", pace_m, pace_s);
                }
            } else {
                lv_label_set_text(label_speed_pace, "-:--");
                if (set_mode_copy != SET_MODE_SPEED) lv_label_set_text(label_speed_pace_set, "-:--");
            }
            prev_pace_int = pace_m;
            prev_pace_frac = pace_s;
        }

        // Pulse
        int current_pulse_copy = (ble_connected_copy && real_pulse_copy > 0) ? real_pulse_copy : -1;
        bool pulse_connected_copy = ble_connected_copy && real_pulse_copy > 0;

        if (current_pulse_copy != prev_pulse || pulse_connected_copy != prev_pulse_connected) {
            if (pulse_connected_copy) {
                lv_label_set_text_fmt(label_pulse, "%d", real_pulse_copy);
                lv_label_set_text_fmt(label_pulse_set, "%d", real_pulse_copy);
            } else {
                lv_label_set_text(label_pulse, "--");
                lv_label_set_text(label_pulse_set, "--");
            }
            prev_pulse = current_pulse_copy;
            prev_pulse_connected = pulse_connected_copy;
        }

        // Kcal
        static bool prev_weight_entered = false;
        if (current_kcal != prev_kcal || weight_entered_copy != prev_weight_entered) {
            if (weight_entered_copy) {
                // Si hay peso introducido, mostrar valor numérico
                lv_label_set_text_fmt(label_kcal, "%d", current_kcal);
            } else {
                // Si no hay peso, mostrar "--"
                lv_label_set_text(label_kcal, "--");
            }
            if (set_mode_copy != SET_MODE_WEIGHT) {
                if (weight_entered_copy) {
                    lv_label_set_text_fmt(label_kcal_set, "%d", current_kcal);
                } else {
                    lv_label_set_text(label_kcal_set, "--");
                }
            }
            prev_kcal = current_kcal;
            prev_weight_entered = weight_entered_copy;
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

        // --- PROVISIONAL: Contador de pasos en ta_info ---
        if (current_steps_copy != prev_steps) {
            lv_label_set_text_fmt(ta_info, "PASOS: %"PRIu32, current_steps_copy);
            prev_steps = current_steps_copy;
        }

        bsp_display_unlock();


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
            // Cambiar la unidad de "kg" a "Kcal" ahora que empezamos a contar calorías
            if (showing_weight_in_kcal_field) {
                lv_label_set_text(unit_kcal_main, "Kcal");  // Cambiar unidad en pantalla MAIN
                lv_label_set_text(label_kcal, "0");  // Cambiar el peso por 0 Kcal
                showing_weight_in_kcal_field = false;
            }
        }

        bsp_display_unlock();
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
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (code == LV_EVENT_PRESSED) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.is_adjusting_speed = true;
        xSemaphoreGive(g_state_mutex);
        ui_speed_inc();
    } 
    else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        ui_speed_inc();
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.is_adjusting_speed = false;
        g_treadmill_state.speed_adjustment_end_ms = now;
        xSemaphoreGive(g_state_mutex);
        ui_speed_execute();
    }
}

static void speed_dec_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (code == LV_EVENT_PRESSED) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.is_adjusting_speed = true;
        xSemaphoreGive(g_state_mutex);
        ui_speed_dec();
    } 
    else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        ui_speed_dec();
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_treadmill_state.is_adjusting_speed = false;
        g_treadmill_state.speed_adjustment_end_ms = now;
        xSemaphoreGive(g_state_mutex);
        ui_speed_execute();
    }
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

void ui_finish_training(void) {
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

    bsp_display_lock(0);
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
    bsp_display_unlock();
}

static void end_event_cb(lv_event_t *e) {
    static TickType_t last_click_tick = 0;
    TickType_t current_tick = xTaskGetTickCount();
    const TickType_t debounce_ticks = pdMS_TO_TICKS(200); // 200ms debounce

    if (current_tick - last_click_tick < debounce_ticks) {
        return;
    }

    last_click_tick = current_tick;
    audio_play_beep();
    ui_finish_training();
}

static void stop_from_cooldown_event_cb(lv_event_t *e) {
    static TickType_t last_click_tick = 0;
    TickType_t current_tick = xTaskGetTickCount();
    const TickType_t debounce_ticks = pdMS_TO_TICKS(200); // 200ms debounce

    if (current_tick - last_click_tick < debounce_ticks) {
        return;
    }

    last_click_tick = current_tick;
    ui_stop_from_cooldown(); 
}

static void ui_stop_from_cooldown(void) {
    audio_play_beep();
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.is_stopped = true;
    g_treadmill_state.is_cooling_down = false;
    g_treadmill_state.is_resuming = false;
    // Guardamos la velocidad actual como la velocidad a la que reanudar si se pulsa RESUME
    g_treadmill_state.speed_before_stop = g_treadmill_state.target_speed;
    g_treadmill_state.target_speed = 0.0f;
    g_treadmill_state.ramp_mode = RAMP_MODE_STOP_STOP;
    xSemaphoreGive(g_state_mutex);
    
    cm_master_set_speed(0.0f);

    // --- ACTUALIZACIÓN DE UI A MODO PAUSA ---
    bsp_display_lock(0);
    lv_label_set_text(label_stop_btn, "RESUME");
    lv_label_set_text(label_cooldown_btn, "END");
    
    lv_obj_remove_event_cb(btn_stop, stop_resume_event_cb);
    lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
    lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
    lv_obj_remove_event_cb(btn_cooldown, stop_from_cooldown_event_cb);
    lv_obj_add_event_cb(btn_cooldown, end_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Estilo rojo para el botón END (tal cual hace el STOP normal)
    lv_obj_set_style_bg_color(btn_cooldown, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_cooldown, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_cooldown_btn, lv_color_hex(0xFFFFFF), 0);
    
    set_info_text_persistent("Ejercicio en pausa. Pulsa RESUME para continuar o END para finalizar.");

    lv_obj_invalidate(btn_stop);
    lv_obj_invalidate(btn_cooldown);
    update_button_states_visual();
    bsp_display_unlock();
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

    // Parar telemetría y obtener el reporte HD
    ia_telemetry_stop_session();
    const char *telemetry_hd = ia_telemetry_get_current_report();

    if (telemetry_hd) {
        const char *user = (training == 2) ? "Itsaso" : "Ina";
        ia_sync_upload_report(user, g_current_plan.plan_id, telemetry_hd, on_report_sent);
    } else {
        ESP_LOGE(TAG, "No telemetry data found to upload!");
        set_info_text_persistent("Error: No hay datos de telemetria.");
        lv_scr_load(scr_training_select);
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
    ui_handle_numpad_press(txt[0]);
}

//==================================================================================
// 6. FUNCIONES DE CREACIÓN DE INTERFAZ
//==================================================================================
static lv_style_t style_title, style_title_column, style_value_main, style_value_secondary, style_unit, style_value_extra_large, style_btn_symbol, style_btn_text, style_btn_premium, style_btn_text_disabled, style_btn_premium_disabled;
static void create_styles(void) {
    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_40);
    lv_style_set_text_color(&style_title, lv_color_hex(0xFFFFFF)); // White for dark background

    lv_style_init(&style_title_column);
    lv_style_set_text_font(&style_title_column, &lv_font_montserrat_20);
    lv_style_set_text_color(&style_title_column, lv_color_hex(0xFFFFFF));

    lv_style_init(&style_value_secondary);
    lv_style_set_text_font(&style_value_secondary, &lv_font_montserrat_40);

    lv_style_init(&style_unit);
    lv_style_set_text_font(&style_unit, &lv_font_montserrat_18);
    lv_style_set_text_color(&style_unit, lv_color_hex(0x888888));

    lv_style_init(&style_btn_symbol);
    lv_style_set_text_font(&style_btn_symbol, &lv_font_montserrat_44);

    lv_style_init(&style_btn_text);
    lv_style_set_text_font(&style_btn_text, &lv_font_montserrat_26);
    lv_style_set_text_color(&style_btn_text, lv_color_hex(0xFFFFFF)); // Blanco por defecto

    lv_style_init(&style_btn_text_disabled);
    lv_style_set_text_color(&style_btn_text_disabled, lv_color_hex(0x000000)); // Negro cuando está desactivo
    lv_style_set_text_opa(&style_btn_text_disabled, LV_OPA_COVER); // Sin difuminado

    lv_style_init(&style_btn_symbol);
    lv_style_set_text_font(&style_btn_symbol, &lv_font_montserrat_44);
    lv_style_set_text_color(&style_btn_symbol, lv_color_hex(0xFFFFFF));

    lv_style_init(&style_btn_premium);
    lv_style_set_bg_color(&style_btn_premium, lv_color_hex(0x2C2C2C)); 
    lv_style_set_border_color(&style_btn_premium, lv_color_hex(0x4A4A4A));
    lv_style_set_border_width(&style_btn_premium, 2);
    lv_style_set_radius(&style_btn_premium, 12);
    lv_style_set_bg_opa(&style_btn_premium, LV_OPA_COVER);

    // Estilo para forzar que el desactivado NO sea difuminado
    lv_style_init(&style_btn_premium_disabled);
    lv_style_set_bg_color(&style_btn_premium_disabled, lv_color_hex(0x2C2C2C)); 
    lv_style_set_bg_opa(&style_btn_premium_disabled, LV_OPA_COVER);
    lv_style_set_opa(&style_btn_premium_disabled, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_premium_disabled, lv_color_hex(0x4A4A4A));
    lv_style_set_border_width(&style_btn_premium_disabled, 2);
    lv_style_set_radius(&style_btn_premium_disabled, 12);
    
    // Aplicar estilo premium a los botones
    lv_style_init(&style_value_extra_large);
    lv_style_set_text_font(&style_value_extra_large, &chivo_mono_100);
    lv_style_set_text_color(&style_value_extra_large, lv_color_hex(0xFFFFFF)); // White for dark background

    lv_style_init(&style_value_main);
    lv_style_set_text_font(&style_value_main, &chivo_mono_70);
    lv_style_set_text_color(&style_value_main, lv_color_hex(0xFFFFFF)); // White for dark background
}

static UIPanels create_common_ui_elements(lv_obj_t *parent) {
    UIPanels panels;

    // TIME (principal) - Subido 4mm (22px)
    panels.time_label = lv_label_create(parent);
    lv_obj_add_style(panels.time_label, &style_value_main, 0);
    lv_obj_align(panels.time_label, LV_ALIGN_CENTER, 90, -171); // Lowered 2px more (total 5px down)

    lv_obj_t* unit_time = lv_label_create(parent);
    lv_obj_add_style(unit_time, &style_unit, 0);
    lv_label_set_text(unit_time, "Tiempo");
    lv_obj_align_to(unit_time, panels.time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    // ZANCADA (izquierda de Time)
    panels.stride_label = lv_label_create(parent);
    lv_obj_add_style(panels.stride_label, &style_value_main, 0);
    lv_label_set_text(panels.stride_label, "123");
    lv_obj_align_to(panels.stride_label, panels.time_label, LV_ALIGN_OUT_LEFT_MID, -210, 0); // Moved 1cm more left (-155-55)
    lv_obj_set_width(panels.stride_label, 150);
    lv_obj_set_style_text_align(panels.stride_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_stride = lv_label_create(parent);
    lv_obj_add_style(unit_stride, &style_unit, 0);
    lv_label_set_text(unit_stride, "Zancada");
    lv_obj_align_to(unit_stride, panels.stride_label, LV_ALIGN_OUT_BOTTOM_LEFT, -2, 5);
    lv_obj_set_width(unit_stride, 150);
    lv_obj_set_style_text_align(unit_stride, LV_TEXT_ALIGN_RIGHT, 0);

    // KCAL (izquierda, encima de las horas de Time) - Movido 1mm (6px) a la izquierda
    panels.kcal_label = lv_label_create(parent);
    lv_obj_add_style(panels.kcal_label, &style_value_main, 0);
    lv_label_set_text(panels.kcal_label, "--");
    lv_obj_align_to(panels.kcal_label, panels.time_label, LV_ALIGN_OUT_TOP_LEFT, -365, -40); // Compensated for time shift (-363-2)
    lv_obj_set_width(panels.kcal_label, 250);
    lv_obj_set_style_text_align(panels.kcal_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_kcal = lv_label_create(parent);
    lv_obj_add_style(unit_kcal, &style_unit, 0);
    lv_label_set_text(unit_kcal, "Kcal");
    lv_obj_align_to(unit_kcal, panels.kcal_label, LV_ALIGN_OUT_BOTTOM_LEFT, -1, 5);
    lv_obj_set_width(unit_kcal, 250);
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
    lv_label_set_text(panels.dist_label, "0");
    lv_obj_align_to(panels.dist_label, panels.time_label, LV_ALIGN_OUT_TOP_RIGHT, -157, -40); // Compensated for time shift (-155-2)
    lv_obj_set_width(panels.dist_label, 300);
    lv_obj_set_style_text_align(panels.dist_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_dist = lv_label_create(parent);
    lv_obj_add_style(unit_dist, &style_unit, 0);
    lv_label_set_text(unit_dist, "Distancia");
    lv_obj_align_to(unit_dist, panels.dist_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_dist, 300);
    lv_obj_set_style_text_align(unit_dist, LV_TEXT_ALIGN_RIGHT, 0);

    // --- COLUMNA DE INCLINACIÓN (CLIMB) --- - Subido 4mm (22px)
    lv_obj_t *label_climb_title = lv_label_create(parent);
    lv_obj_add_style(label_climb_title, &style_title_column, 0);
    lv_obj_set_style_text_color(label_climb_title, lv_color_hex(0x888888), 0); // Gray like "Percent"
    lv_label_set_text(label_climb_title, "PENDIENTE");
    lv_obj_align(label_climb_title, LV_ALIGN_TOP_LEFT, 125, 30); // Lowered 2px more (total 5px down)
    lv_obj_set_width(label_climb_title, 180);
    lv_obj_set_style_text_align(label_climb_title, LV_TEXT_ALIGN_RIGHT, 0);
    
    panels.climb_percent_label = lv_label_create(parent);
    lv_obj_add_style(panels.climb_percent_label, &style_value_main, 0);
    lv_obj_align_to(panels.climb_percent_label, label_climb_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_width(panels.climb_percent_label, 180);
    lv_obj_set_style_text_align(panels.climb_percent_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_percent = lv_label_create(parent);
    lv_obj_add_style(unit_percent, &style_unit, 0);
    lv_label_set_text(unit_percent, "Porcentaje");
    lv_obj_align_to(unit_percent, panels.climb_percent_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);
    lv_obj_set_width(unit_percent, 180);
    lv_obj_set_style_text_align(unit_percent, LV_TEXT_ALIGN_RIGHT, 0);
    
    panels.pulse_label = lv_label_create(parent);
    lv_obj_add_style(panels.pulse_label, &style_value_main, 0);
    lv_label_set_text(panels.pulse_label, "--");  // Texto inicial cuando no hay sensor
    lv_obj_align_to(panels.pulse_label, unit_percent, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_width(panels.pulse_label, 180);
    lv_obj_set_style_text_align(panels.pulse_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* unit_pulse = lv_label_create(parent);
    lv_obj_add_style(unit_pulse, &style_unit, 0);
    lv_label_set_text(unit_pulse, "Pulso");
    lv_obj_align_to(unit_pulse, panels.pulse_label, LV_ALIGN_OUT_BOTTOM_LEFT, -1, 5);
    lv_obj_set_width(unit_pulse, 180);
    lv_obj_set_style_text_align(unit_pulse, LV_TEXT_ALIGN_RIGHT, 0);

    // --- COLUMNA DE VELOCIDAD (SPEED) --- - Subido 4mm (22px)
    lv_obj_t *label_speed_title = lv_label_create(parent);
    lv_obj_add_style(label_speed_title, &style_title_column, 0);
    lv_obj_set_style_text_color(label_speed_title, lv_color_hex(0x888888), 0); // Gray like "Percent"
    lv_label_set_text(label_speed_title, "VELOCIDAD");
    lv_obj_align(label_speed_title, LV_ALIGN_TOP_RIGHT, -186, 30); // Lowered 2px more (total 5px down)
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

    // --- INFO BOX --- - Subido 4mm (22px)
    panels.info_label = lv_label_create(parent);
    lv_obj_add_style(panels.info_label, &style_title, 0);
    lv_obj_set_style_text_color(panels.info_label, lv_color_hex(0x000000), 0); // Black text inside gray box
    lv_obj_set_size(panels.info_label, 840, 190);
    lv_obj_align(panels.info_label, LV_ALIGN_CENTER, 0, 27); // Raised 3px (30-3)
    lv_obj_set_style_text_align(panels.info_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_all(panels.info_label, 10, 0);
    lv_obj_set_style_bg_color(panels.info_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_bg_opa(panels.info_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panels.info_label, 12, 0);
    lv_obj_set_style_border_color(panels.info_label, lv_color_hex(0xCCCCCC), 0);
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

    // DESACTIVAR TRAINING MODE (saliendo de pantalla principal)
    cm_master_set_training_mode(false);
    ia_telemetry_stop_session();
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.plan_running = false;
    xSemaphoreGive(g_state_mutex);
    ESP_LOGI(TAG, "Saliendo de pantalla principal - Training mode, Telemetría y Plan desactivados");

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

static void app_settings_event_cb(lv_event_t *e) {
    audio_play_beep();
    lv_scr_load(scr_app_settings);
}

static void app_settings_back_event_cb(lv_event_t *e) {
    audio_play_beep();
    lv_scr_load(scr_training_select);
}

static void brightness_arc_event_cb(lv_event_t *e) {
    lv_obj_t * arc = lv_event_get_target(e);
    int val = lv_arc_get_value(arc); // Actualizar label
    lv_label_set_text_fmt(label_brightness_pct, "%d%%", val);
    
    // Nota: El bsp_display_brightness_set del P4 EVB suele aceptar 0-100 porcentual
    bsp_display_brightness_set(val);
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.display_brightness = val;
    xSemaphoreGive(g_state_mutex);
}



static void volume_arc_event_cb(lv_event_t *e) {
    lv_obj_t * arc = lv_event_get_target(e);
    int val = lv_arc_get_value(arc);
    lv_label_set_text_fmt(label_volume_pct, "%d%%", val);
    audio_set_volume((uint8_t)val);
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.audio_volume = (uint8_t)val;
    xSemaphoreGive(g_state_mutex);
}


static void sensitivity_slider_event_cb(lv_event_t *e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    lv_label_set_text_fmt(label_sensitivity_pct, "%d%%", val);
    
    imu_service_set_sensitivity((uint8_t)val);
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.pedometer_sensitivity = (uint8_t)val;
    xSemaphoreGive(g_state_mutex);
    
    save_app_setting_to_nvs(NVS_KEY_SENSITIVITY, (uint8_t)val);
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

    // ACTIVAR TRAINING MODE (entrando a pantalla principal)
    cm_master_set_training_mode(true);
    ESP_LOGI(TAG, "Entrando a pantalla principal - Training mode activado");

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
    ia_sync_get_next_plan("Itsaso", on_plan_received);
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
    ia_sync_get_next_plan("Ina", on_plan_received);
}


//==================================================================================
// CREACIÓN DE PANTALLA DE SELECCIÓN
//==================================================================================
static void create_training_select_screen(void) {
    scr_training_select = lv_obj_create(NULL);
    lv_obj_set_size(scr_training_select, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_training_select, LV_OBJ_FLAG_SCROLLABLE);

    // Botones anchos pegados a la izquierda (igual que pantalla principal)
    const int left_btn_w = 350;
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
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(btn_container, margin, 0);
    lv_obj_set_style_pad_bottom(btn_container, margin, 0);
    lv_obj_set_style_pad_gap(btn_container, 20, 0);

    // Botón 1: Entrenamiento libre
    btn = lv_btn_create(btn_container);
    lv_obj_set_size(btn, left_btn_w, btn_h);
    lv_obj_add_style(btn, &style_btn_premium, 0); 
    lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED); 
    lv_obj_add_event_cb(btn, training_free_event_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(l, 0, 0); // Asegurar que el label no tenga fondo (evita recuadro gris)
    lv_label_set_text(l, "Entrenamiento libre");
    lv_obj_center(l);

    btn_training_itsaso = lv_btn_create(btn_container);
    lv_obj_set_size(btn_training_itsaso, left_btn_w, btn_h);
    lv_obj_add_style(btn_training_itsaso, &style_btn_premium, 0); 
    lv_obj_add_style(btn_training_itsaso, &style_btn_premium_disabled, LV_STATE_DISABLED); 
    lv_obj_add_event_cb(btn_training_itsaso, training_itsaso_event_cb, LV_EVENT_CLICKED, NULL);
    label_training_itsaso = lv_label_create(btn_training_itsaso);
    lv_obj_add_style(label_training_itsaso, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(label_training_itsaso, 0, 0); 
    lv_label_set_text(label_training_itsaso, "Conectando...");
    lv_obj_center(label_training_itsaso);
    lv_obj_clear_flag(btn_training_itsaso, LV_OBJ_FLAG_CLICKABLE); // Deshabilitado lógicamente
    lv_obj_set_style_text_color(label_training_itsaso, lv_color_hex(0x000000), 0); // Negro inicial

    // Botón 3: Entrenamiento Ina
    btn_training_ina = lv_btn_create(btn_container);
    lv_obj_set_size(btn_training_ina, left_btn_w, btn_h);
    lv_obj_add_style(btn_training_ina, &style_btn_premium, 0); 
    lv_obj_add_style(btn_training_ina, &style_btn_premium_disabled, LV_STATE_DISABLED); 
    lv_obj_add_event_cb(btn_training_ina, training_ina_event_cb, LV_EVENT_CLICKED, NULL);
    label_training_ina = lv_label_create(btn_training_ina);
    lv_obj_add_style(label_training_ina, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(label_training_ina, 0, 0); 
    lv_label_set_text(label_training_ina, "Conectando...");
    lv_obj_center(label_training_ina);
    lv_obj_clear_flag(btn_training_ina, LV_OBJ_FLAG_CLICKABLE); // Deshabilitado lógicamente
    lv_obj_set_style_text_color(label_training_ina, lv_color_hex(0x000000), 0); // Negro inicial


    // --- NEW: Dark background ---
    lv_obj_set_style_bg_color(scr_training_select, lv_color_black(), 0);

    // --- NEW: Right column for numbered buttons ---
    const int right_btn_w = 350;
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

    // Create only the 4 required buttons
    for (int i = 1; i <= 4; i++) {
        btn = lv_btn_create(right_col);
        lv_obj_set_size(btn, right_btn_w, btn_h);
        lv_obj_add_style(btn, &style_btn_premium, 0); 
        lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED); 
        lv_obj_set_user_data(btn, (void*)i);

        // Set event callbacks
        if (i == 1) {
            lv_obj_add_event_cb(btn, wifi_selector_event_cb, LV_EVENT_CLICKED, NULL);
        } else if (i == 2) {
            lv_obj_add_event_cb(btn, ble_scan_button_event_cb, LV_EVENT_CLICKED, NULL);
        } else if (i == 3) {
            lv_obj_add_event_cb(btn, wax_event_cb, LV_EVENT_CLICKED, NULL);
        } else {
            lv_obj_add_event_cb(btn, app_settings_event_cb, LV_EVENT_CLICKED, NULL);
        }

        l = lv_label_create(btn);
        lv_obj_add_style(l, &style_btn_text, 0);
        lv_obj_set_style_bg_opa(l, 0, 0); 

        // Set labels
        if (i == 1) {
            lv_label_set_text(l, "Ajustes WiFi");
        } else if (i == 2) {
            lv_label_set_text(l, "Ajustes BLE");
        } else if (i == 3) {
            lv_label_set_text(l, "Ajustes WAX");
        } else {
            lv_label_set_text(l, "Ajustes APP");
        }
        lv_obj_center(l);
    }

    btn_upload_training = lv_btn_create(right_col);
    lv_obj_set_size(btn_upload_training, right_btn_w, btn_h);
    lv_obj_add_style(btn_upload_training, &style_btn_premium, 0); 
    lv_obj_add_style(btn_upload_training, &style_btn_premium_disabled, LV_STATE_DISABLED); 
    lv_obj_add_event_cb(btn_upload_training, upload_training_event_cb, LV_EVENT_CLICKED, NULL);
    // Aplicar color rojo sólido
    lv_obj_set_style_bg_color(btn_upload_training, lv_color_hex(0xCC0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_upload_training, lv_color_hex(0xCC0000), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_opa(btn_upload_training, LV_OPA_COVER, LV_STATE_DISABLED); // Forzar opacidad total
    l = lv_label_create(btn_upload_training);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(l, 0, 0); 
    lv_label_set_text(l, "UPLOAD");
    lv_obj_center(l);
    // Ocultar por defecto (solo se muestra para entrenamientos 2 y 3)
    lv_obj_add_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);

    // --- WiFi and BLE Status Labels (Bottom Center) ---
    label_status_wifi = lv_label_create(scr_training_select);
    lv_obj_add_style(label_status_wifi, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_status_wifi, &lv_font_montserrat_18, 0); // Smaller font ONLY for this object
    lv_label_set_text(label_status_wifi, "WiFi: No conectado");
    lv_obj_align(label_status_wifi, LV_ALIGN_BOTTOM_MID, 0, -20);

    label_status_ble = lv_label_create(scr_training_select);
    lv_obj_add_style(label_status_ble, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_status_ble, &lv_font_montserrat_18, 0); // Equal to WiFi status
    lv_label_set_text(label_status_ble, "Cardio BLE: No conectado");
    lv_obj_align(label_status_ble, LV_ALIGN_BOTTOM_MID, 0, -50);

    // Crear timer para verificar estado de WiFi cada 100ms
    wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 100, NULL);

    // --- ICONO CANAL CENTRAL ---
    lv_obj_t *img_icon = lv_img_create(scr_training_select);
    lv_img_set_src(img_icon, &icon_main);
    lv_obj_align(img_icon, LV_ALIGN_CENTER, 0, -150);
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
    
    // Dark background (matching scr_training_select)
    lv_obj_set_style_bg_color(scr_main, lv_color_black(), 0);
    
    UIPanels panels = create_common_ui_elements(scr_main);
    lv_obj_t *btn, *l;
    
    label_dist = panels.dist_label;
    label_time = panels.time_label;
    label_climb_percent = panels.climb_percent_label;
    label_speed_kmh = panels.speed_kmh_label;
    label_speed_pace = panels.speed_pace_label;
    label_pulse = panels.pulse_label;
    label_kcal = panels.kcal_label;
    label_stride = panels.stride_label;
    ta_info = panels.info_label;

    // --- CUADRO GRIS OSCURO (Debajo del cuadro de texto) ---
    lv_obj_t *dark_box = lv_obj_create(scr_main);
    lv_obj_set_size(dark_box, 899, 190); // 100 columns of 8px + 99 lines of 1px = 899px
    lv_obj_align_to(dark_box, ta_info, LV_ALIGN_OUT_BOTTOM_MID, 0, 48); // Raised by 6px (1mm)
    lv_obj_set_style_bg_color(dark_box, lv_color_hex(0x2C2C2C), 0);
    lv_obj_set_style_bg_opa(dark_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dark_box, 0, 0); // Sharp corners as requested
    lv_obj_set_style_border_width(dark_box, 0, 0);
    lv_obj_set_style_pad_all(dark_box, 0, 0); // Crucial for pixel-perfect alignment
    lv_obj_clear_flag(dark_box, LV_OBJ_FLAG_SCROLLABLE);

    // División en 100 franjas (99 líneas) - Todas exactamente iguales (8px de espacio + 1px de línea)
    for (int i = 1; i < 100; i++) {
        lv_obj_t *v_line = lv_obj_create(dark_box);
        lv_obj_set_size(v_line, 1, 190);
        lv_obj_set_pos(v_line, i * 9 - 1, 0); // Stride de 9px garantiza 8px de espacio entre líneas
        lv_obj_set_style_bg_color(v_line, lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_opa(v_line, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(v_line, 0, 0);
        lv_obj_set_style_radius(v_line, 0, 0);
    }

    // Crear objetos para los puntos de potencia (inicialmente ocultos)
    for (int i = 0; i < 100; i++) {
        power_dots[i] = lv_obj_create(dark_box);
        lv_obj_set_size(power_dots[i], 4, 4); 
        lv_obj_set_style_bg_color(power_dots[i], lv_color_hex(0x00FF00), 0); 
        lv_obj_set_style_bg_opa(power_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(power_dots[i], 0, 0);
        lv_obj_set_style_radius(power_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(power_dots[i], LV_OBJ_FLAG_HIDDEN); 
        lv_obj_set_pos(power_dots[i], i * 9 + 2, 0); 
    }

    // Crear la línea continua que une los puntos
    power_line = lv_line_create(dark_box);
    lv_obj_set_style_line_width(power_line, 1, 0); // Línea fina
    lv_obj_set_style_line_color(power_line, lv_color_hex(0x00FF00), 0); // Mismo verde
    lv_obj_set_style_line_opa(power_line, LV_OPA_COVER, 0);
    lv_obj_add_flag(power_line, LV_OBJ_FLAG_HIDDEN); // Oculta al inicio

    lv_obj_t *label_0m = lv_label_create(scr_main);
    lv_obj_add_style(label_0m, &style_unit, 0);
    lv_label_set_text(label_0m, "0 m");
    lv_obj_align_to(label_0m, dark_box, LV_ALIGN_OUT_BOTTOM_LEFT, -11, 5); // Moved 2mm left

    label_1000m = lv_label_create(scr_main);
    lv_obj_add_style(label_1000m, &style_unit, 0);
    lv_label_set_text(label_1000m, "1.000 m");
    lv_obj_align_to(label_1000m, dark_box, LV_ALIGN_OUT_BOTTOM_RIGHT, 34, 5); // Moved 5px left (39-5)

    const int btn_w = 120, btn_h = 136;
    const int margin = 20;

    lv_obj_t * left_col = lv_obj_create(scr_main);
    lv_obj_remove_style_all(left_col);
    lv_obj_set_size(left_col, btn_w, LV_PCT(100));
    lv_obj_align(left_col, LV_ALIGN_TOP_LEFT, margin, 0);
    lv_obj_set_layout(left_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(left_col, margin, 0);
    lv_obj_set_style_pad_bottom(left_col, margin, 0);

    btn_climb_inc = lv_btn_create(left_col); lv_obj_set_size(btn_climb_inc, btn_w, btn_h); lv_obj_add_style(btn_climb_inc, &style_btn_premium, 0); lv_obj_add_style(btn_climb_inc, &style_btn_premium_disabled, LV_STATE_DISABLED); lv_obj_add_event_cb(btn_climb_inc, climb_inc_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_climb_inc); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_PLUS); lv_obj_center(l);
    btn_climb_set = lv_btn_create(left_col); lv_obj_set_size(btn_climb_set, btn_w, btn_h); lv_obj_add_style(btn_climb_set, &style_btn_premium, 0); lv_obj_add_style(btn_climb_set, &style_btn_premium_disabled, LV_STATE_DISABLED); lv_obj_add_event_cb(btn_climb_set, set_climb_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_climb_set); lv_obj_add_style(l, &style_btn_text, 0); lv_label_set_text(l, "SELEC"); lv_obj_center(l);
    btn_climb_dec = lv_btn_create(left_col); lv_obj_set_size(btn_climb_dec, btn_w, btn_h); lv_obj_add_style(btn_climb_dec, &style_btn_premium, 0); lv_obj_add_style(btn_climb_dec, &style_btn_premium_disabled, LV_STATE_DISABLED); lv_obj_add_event_cb(btn_climb_dec, climb_dec_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_climb_dec); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_MINUS); lv_obj_center(l);

    // Botón CHEST
    btn = lv_btn_create(left_col);
    lv_obj_set_size(btn, btn_w, btn_h);
    lv_obj_add_style(btn, &style_btn_premium, 0);
    lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED);
    lv_obj_add_event_cb(btn, chest_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(l, 0, 0); // Asegurar que el label no tenga fondo (evita recuadro gris)
    lv_label_set_text(l, "VENT.\nPECHO");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón
    label_chest_value = lv_label_create(btn);
    lv_obj_add_style(label_chest_value, &style_btn_text, 0);
    lv_label_set_text(label_chest_value, "0");
    lv_obj_add_flag(label_chest_value, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón

    btn_stop = lv_btn_create(left_col); lv_obj_set_size(btn_stop, btn_w, btn_h); lv_obj_add_style(btn_stop, &style_btn_premium, 0); lv_obj_add_style(btn_stop, &style_btn_premium_disabled, LV_STATE_DISABLED);
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
    
    btn_speed_inc = lv_btn_create(right_col); lv_obj_set_size(btn_speed_inc, btn_w, btn_h); lv_obj_add_style(btn_speed_inc, &style_btn_premium, 0); lv_obj_add_style(btn_speed_inc, &style_btn_premium_disabled, LV_STATE_DISABLED); lv_obj_add_event_cb(btn_speed_inc, speed_inc_event_cb, LV_EVENT_ALL, NULL); l = lv_label_create(btn_speed_inc); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_PLUS); lv_obj_center(l);
    btn_speed_set = lv_btn_create(right_col); lv_obj_set_size(btn_speed_set, btn_w, btn_h); lv_obj_add_style(btn_speed_set, &style_btn_premium, 0); lv_obj_add_style(btn_speed_set, &style_btn_premium_disabled, LV_STATE_DISABLED); lv_obj_add_event_cb(btn_speed_set, set_speed_event_cb, LV_EVENT_CLICKED, NULL); l = lv_label_create(btn_speed_set); lv_obj_add_style(l, &style_btn_text, 0); lv_label_set_text(l, "SELEC"); lv_obj_center(l);
    btn_speed_dec = lv_btn_create(right_col); lv_obj_set_size(btn_speed_dec, btn_w, btn_h); lv_obj_add_style(btn_speed_dec, &style_btn_premium, 0); lv_obj_add_style(btn_speed_dec, &style_btn_premium_disabled, LV_STATE_DISABLED); lv_obj_add_event_cb(btn_speed_dec, speed_dec_event_cb, LV_EVENT_ALL, NULL); l = lv_label_create(btn_speed_dec); lv_obj_add_style(l, &style_btn_symbol, 0); lv_label_set_text(l, LV_SYMBOL_MINUS); lv_obj_center(l);

    // Botón HEAD
    btn = lv_btn_create(right_col);
    lv_obj_set_size(btn, btn_w, btn_h);
    lv_obj_add_style(btn, &style_btn_premium, 0);
    lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED);
    lv_obj_add_event_cb(btn, head_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(l, 0, 0); // Asegurar que el label no tenga fondo (evita recuadro gris)
    lv_label_set_text(l, "VENT.\nCARA");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón
    label_head_value = lv_label_create(btn);
    lv_obj_add_style(label_head_value, &style_btn_text, 0);
    lv_label_set_text(label_head_value, "0");
    lv_obj_add_flag(label_head_value, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botón

    btn_cooldown = lv_btn_create(right_col); lv_obj_set_size(btn_cooldown, btn_w, btn_h); lv_obj_add_style(btn_cooldown, &style_btn_premium, 0); lv_obj_add_style(btn_cooldown, &style_btn_premium_disabled, LV_STATE_DISABLED);
    label_cooldown_btn = lv_label_create(btn_cooldown);
    lv_obj_add_style(label_cooldown_btn, &style_btn_text, 0);
    lv_label_set_text(label_cooldown_btn, "COOL\nDOWN");
    lv_obj_set_style_text_align(label_cooldown_btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label_cooldown_btn);

    // Siempre empezar con BACK (izquierda) y WEIGHT (derecha)
    lv_label_set_text(label_stop_btn, "ATRAS");
    lv_label_set_text(label_cooldown_btn, "PESO");
    lv_obj_add_event_cb(btn_stop, back_to_training_select_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_cooldown, weight_event_cb, LV_EVENT_CLICKED, NULL);

    // Inicializar estado visual de botones (al inicio todos deben estar habilitados)
    update_button_states_visual();
}

static void create_set_screen(void) {
    scr_set = lv_obj_create(NULL);
    lv_obj_set_size(scr_set, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(scr_set, LV_OBJ_FLAG_SCROLLABLE);
    
    // Dark background (matching scr_training_select)
    lv_obj_set_style_bg_color(scr_set, lv_color_black(), 0);
    
    UIPanels panels = create_common_ui_elements(scr_set);
    label_dist_set = panels.dist_label;
    label_time_set = panels.time_label;
    label_climb_percent_set = panels.climb_percent_label;
    label_speed_kmh_set = panels.speed_kmh_label;
    label_speed_pace_set = panels.speed_pace_label;
    label_pulse_set = panels.pulse_label;
    label_kcal_set = panels.kcal_label;
    label_stride_set = panels.stride_label;
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
        lv_obj_add_style(btn, &style_btn_premium, 0);
        lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED);
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
        lv_obj_add_style(btn, &style_btn_premium, 0);
        lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED);
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

// Callback para timeout de entrada de velocidad (5 segundos)
static void speed_input_timeout_cb(lv_timer_t *timer) {
    waiting_for_second_digit = false;
    if (speed_input_timeout_timer) {
        lv_timer_del(speed_input_timeout_timer);
        speed_input_timeout_timer = NULL;
    }
    
    // Interpretar '1' como 1 km/h
    g_treadmill_state.set_buffer[0] = '1';
    g_treadmill_state.set_buffer[1] = '\0';

    g_treadmill_state.set_digit_index = 1;
    
    // Confirmar automáticamente
    ui_confirm_set_value();
}

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
        // Para velocidad usamos 2 dígitos sin decimal
        char d1 = (g_treadmill_state.set_digit_index > 0) ? g_treadmill_state.set_buffer[0] : '-';
        char d2 = (g_treadmill_state.set_digit_index > 1) ? g_treadmill_state.set_buffer[1] : '-';

        char cursor = g_treadmill_state.blink_state ? '-' : ' ';
        if (g_treadmill_state.set_digit_index == 0) d1 = cursor;
        else if (g_treadmill_state.set_digit_index == 1) d2 = cursor;

        sprintf(display_buf, "%c%c", d1, d2);
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
    // Limpiar timer de timeout de velocidad si existe
    if (speed_input_timeout_timer) {
        lv_timer_del(speed_input_timeout_timer);
        speed_input_timeout_timer = NULL;
    }
    waiting_for_second_digit = false;

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

static void create_app_settings_screen(void) {
    scr_app_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_app_settings, lv_color_black(), 0);
    
    lv_obj_t * title = lv_label_create(scr_app_settings);
    lv_obj_add_style(title, &style_btn_text, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_label_set_text(title, "AJUSTES APP");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    // --- DIAL DE BRILLO ---
    lv_obj_t * cont_br = lv_obj_create(scr_app_settings);
    lv_obj_set_size(cont_br, 350, 400); // reduced from 450
    lv_obj_set_style_bg_opa(cont_br, 0, 0);
    lv_obj_set_style_border_opa(cont_br, 0, 0);
    lv_obj_align(cont_br, LV_ALIGN_CENTER, -360, -40); // Shifted up (was 20)
    lv_obj_clear_flag(cont_br, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * l_br = lv_label_create(cont_br);
    lv_obj_add_style(l_br, &style_btn_text, 0);
    lv_label_set_text(l_br, "BRILLO");
    lv_obj_align(l_br, LV_ALIGN_TOP_MID, 0, 0);
    
    arc_brightness = lv_arc_create(cont_br);
    lv_obj_set_size(arc_brightness, 280, 280);
    lv_arc_set_rotation(arc_brightness, 135);
    lv_arc_set_bg_angles(arc_brightness, 0, 270);
    lv_arc_set_value(arc_brightness, g_treadmill_state.display_brightness);
    lv_obj_align(arc_brightness, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_event_cb(arc_brightness, brightness_arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    label_brightness_pct = lv_label_create(arc_brightness);
    lv_obj_add_style(label_brightness_pct, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_brightness_pct, &lv_font_montserrat_28, 0);
    lv_label_set_text_fmt(label_brightness_pct, "%d%%", g_treadmill_state.display_brightness);
    lv_obj_center(label_brightness_pct);
    
    // --- DIAL DE VOLUMEN ---
    lv_obj_t * cont_vol = lv_obj_create(scr_app_settings);
    lv_obj_set_size(cont_vol, 350, 400); // reduced from 450
    lv_obj_set_style_bg_opa(cont_vol, 0, 0);
    lv_obj_set_style_border_opa(cont_vol, 0, 0);
    lv_obj_align(cont_vol, LV_ALIGN_CENTER, 360, -40); // Shifted up (was 20)
    lv_obj_clear_flag(cont_vol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * l_vol = lv_label_create(cont_vol);
    lv_obj_add_style(l_vol, &style_btn_text, 0);
    lv_label_set_text(l_vol, "VOLUMEN");
    lv_obj_align(l_vol, LV_ALIGN_TOP_MID, 0, 0);
    
    arc_volume = lv_arc_create(cont_vol);
    lv_obj_set_size(arc_volume, 280, 280);
    lv_arc_set_rotation(arc_volume, 135);
    lv_arc_set_bg_angles(arc_volume, 0, 270);
    lv_arc_set_value(arc_volume, g_treadmill_state.audio_volume);
    lv_obj_align(arc_volume, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_event_cb(arc_volume, volume_arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    label_volume_pct = lv_label_create(arc_volume);
    lv_obj_add_style(label_volume_pct, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_volume_pct, &lv_font_montserrat_28, 0);
    lv_label_set_text_fmt(label_volume_pct, "%d%%", g_treadmill_state.audio_volume);
    lv_obj_center(label_volume_pct);
    
    // --- BOTON ATRAS ---
    lv_obj_t * btn_back = lv_btn_create(scr_app_settings);
    lv_obj_set_size(btn_back, 200, 80);
    lv_obj_add_style(btn_back, &style_btn_premium, 0);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 40, -40);
    lv_obj_add_event_cb(btn_back, app_settings_back_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_obj_add_style(lbl_back, &style_btn_text, 0);
    lv_label_set_text(lbl_back, "VOLVER");
    lv_obj_center(lbl_back);

    // --- ELEVACION MANUAL ---
    lv_obj_t * cont_manual = lv_obj_create(scr_app_settings);
    lv_obj_set_size(cont_manual, 300, 400); // reduced from 450
    lv_obj_set_style_bg_opa(cont_manual, 0, 0);
    lv_obj_set_style_border_opa(cont_manual, 0, 0);
    lv_obj_align(cont_manual, LV_ALIGN_CENTER, 0, -40); // Shifted up (was 20)
    lv_obj_clear_flag(cont_manual, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * l_man = lv_label_create(cont_manual);
    lv_obj_add_style(l_man, &style_btn_text, 0);
    lv_label_set_text(l_man, "ELEVACION MANUAL");
    lv_obj_align(l_man, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t * btn_up = lv_btn_create(cont_manual);
    lv_obj_set_size(btn_up, 180, 100); // Slightly smaller to fit Better
    lv_obj_add_style(btn_up, &style_btn_premium, 0);
    lv_obj_align(btn_up, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_event_cb(btn_up, manual_up_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t * lbl_up = lv_label_create(btn_up);
    lv_obj_add_style(lbl_up, &style_btn_text, 0);
    lv_label_set_text(lbl_up, "SUBIR");
    lv_obj_center(lbl_up);

    lv_obj_t * btn_down = lv_btn_create(cont_manual);
    lv_obj_set_size(btn_down, 180, 100); // Slightly smaller
    lv_obj_add_style(btn_down, &style_btn_premium, 0);
    lv_obj_align(btn_down, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_add_event_cb(btn_down, manual_down_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * lbl_down = lv_label_create(btn_down);
    lv_obj_add_style(lbl_down, &style_btn_text, 0);
    lv_label_set_text(lbl_down, "BAJAR");
    lv_obj_center(lbl_down);

    // --- HORIZONTAL SLIDER FOR SENSITIVITY ---
    lv_obj_t * cont_sens = lv_obj_create(scr_app_settings);
    lv_obj_set_size(cont_sens, 800, 150);
    lv_obj_set_style_bg_opa(cont_sens, 0, 0);
    lv_obj_set_style_border_opa(cont_sens, 0, 0);
    lv_obj_align(cont_sens, LV_ALIGN_BOTTOM_MID, 80, -40); // Offset x=80 to avoid back button
    lv_obj_clear_flag(cont_sens, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * l_sens = lv_label_create(cont_sens);
    lv_obj_add_style(l_sens, &style_btn_text, 0);
    lv_label_set_text(l_sens, "SENSIBLIDAD PODOMETRO");
    lv_obj_align(l_sens, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t * slider_sens = lv_slider_create(cont_sens);
    lv_obj_set_size(slider_sens, 600, 20);
    lv_obj_align(slider_sens, LV_ALIGN_CENTER, -40, 20);
    lv_slider_set_range(slider_sens, 0, 100);
    lv_slider_set_value(slider_sens, g_treadmill_state.pedometer_sensitivity, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_sens, sensitivity_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label_sensitivity_pct = lv_label_create(cont_sens);
    lv_obj_add_style(label_sensitivity_pct, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_sensitivity_pct, &lv_font_montserrat_28, 0);
    lv_label_set_text_fmt(label_sensitivity_pct, "%d%%", g_treadmill_state.pedometer_sensitivity);
    lv_obj_align_to(label_sensitivity_pct, slider_sens, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
}


void ui_init(void) {
    // 1. Inicializar estado y valores de sesión (50% por defecto)
    uint8_t sens_val = 50;
    load_sensitivity_from_nvs(&sens_val);

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.display_brightness = 50;
    g_treadmill_state.audio_volume = 50;
    g_treadmill_state.pedometer_sensitivity = sens_val;
    g_treadmill_state.target_climb_percent = 0.0f;
    g_treadmill_state.total_running_seconds = load_wax_counter_from_nvs();
    g_treadmill_state.cooldown_level = 0;
    g_treadmill_state.is_adjusting_speed = false;
    g_treadmill_state.speed_adjustment_end_ms = 0;
    xSemaphoreGive(g_state_mutex);

    // Aplicar hardware
    bsp_display_brightness_set(50);
    audio_set_volume(50);
    imu_service_set_sensitivity(sens_val);




    // 2. Crear estilos y pantallas
    create_styles();
    create_training_select_screen();
    create_ble_scan_screen();
    create_loading_screen();
    create_uploading_screen();
    create_main_screen();
    create_set_screen();
    create_wax_screen();
    create_shutdown_screen();
    create_wifi_screens();
    create_app_settings_screen(); // Ahora usará los valores de g_treadmill_state que ya son 50

    lv_scr_load(scr_training_select);
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

        // --- LÓGICA SIMPLIFICADA PARA AJUSTE DELAYED ---
        if (g_treadmill_state.target_speed < 0.5f) {
            // Si la velocidad es < 0.5, la primera pulsación salta a 0.5
            new_target_speed = 0.5f;
        } else {
            // Incrementar siempre de 0.1 en 0.1
            new_target_speed = g_treadmill_state.target_speed + 0.1f;
        }
        g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;

        if (new_target_speed > MAX_SPEED_KMH) new_target_speed = MAX_SPEED_KMH;
        g_treadmill_state.target_speed = new_target_speed;
    } else {
        new_target_speed = g_treadmill_state.target_speed; // Maintain the current target if stopped/cooldown
    }
    xSemaphoreGive(g_state_mutex);

    // Enviar comando al esclavo via RS485
    if (should_beep) {
        // cm_master_set_speed(new_target_speed); // Eliminado: se ejecutará al soltar el botón
        audio_play_beep();
    }
}

void ui_speed_dec(void) {
    bool should_beep = false;
    float new_target_speed;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_beep = true;

        // --- LÓGICA SIMPLIFICADA PARA AJUSTE DELAYED ---
        new_target_speed = g_treadmill_state.target_speed - 0.1f;
        g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;

        if (new_target_speed < 0.0f) new_target_speed = 0.0f;
        g_treadmill_state.target_speed = new_target_speed;
    } else {
        new_target_speed = g_treadmill_state.target_speed; // Maintain the current target if stopped/cooldown
    }
    xSemaphoreGive(g_state_mutex);

    // Enviar comando al esclavo via RS485
    if (should_beep) {
        // cm_master_set_speed(new_target_speed); // Eliminado: se ejecutará al soltar el botón
        audio_play_beep();
    }
}

void ui_speed_execute(void) {
    float target_speed;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    target_speed = g_treadmill_state.target_speed;
    xSemaphoreGive(g_state_mutex);

    cm_master_set_speed(target_speed);
    ESP_LOGI("UI", "ui_speed_execute: Comando de velocidad enviado al motor: %.1f km/h", target_speed);
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

    bool was_stopped;
    bool was_cooling_down;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    was_stopped = g_treadmill_state.is_stopped;
    was_cooling_down = g_treadmill_state.is_cooling_down;

    if (was_cooling_down) {
        // RESUME desde Cool Down
        last_speed_ramp_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        g_treadmill_state.is_cooling_down = false;
        g_treadmill_state.is_resuming = true;
        g_treadmill_state.cooldown_level = 0;
        // La velocidad objetivo vuelve a ser la que había antes de empezar el cool down
        float resume_speed = g_treadmill_state.speed_before_stop;
        g_treadmill_state.target_speed = resume_speed;
        g_treadmill_state.ramp_mode = RAMP_MODE_COOLDOWN_RESUME;
        
        ESP_LOGI(TAG, "Reanudando desde Cool Down a %.2f km/h", resume_speed);
        xSemaphoreGive(g_state_mutex);
    } 
    else if (!was_stopped) {
        // Soft STOP (Pausa)
        g_treadmill_state.is_stopped = true;
        g_treadmill_state.is_resuming = false;
        g_treadmill_state.speed_before_stop = g_treadmill_state.target_speed;
        g_treadmill_state.target_speed = 0.0f;
        g_treadmill_state.ramp_mode = RAMP_MODE_STOP_STOP;
        
        ESP_LOGI(TAG, "STOP activado (Pausa)");
        xSemaphoreGive(g_state_mutex);
        cm_master_set_speed(0.0f);
    } else {
        // RESUME desde STOP
        last_speed_ramp_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        g_treadmill_state.is_stopped = false;
        g_treadmill_state.is_resuming = true;
        float resume_speed = g_treadmill_state.speed_before_stop;
        g_treadmill_state.target_speed = resume_speed;
        g_treadmill_state.ramp_mode = RAMP_MODE_STOP_RESUME;
        
        ESP_LOGI(TAG, "RESUME activado desde pausa");
        xSemaphoreGive(g_state_mutex);
        // El incremento gradual se hará en ui_update_task
    }

    // --- ACTUALIZACIÓN DE UI ---
    bsp_display_lock(0);
    if (was_cooling_down || was_stopped) {
        // Volvemos a modo NORMAL
        lv_label_set_text(label_stop_btn, "STOP");
        lv_label_set_text(label_cooldown_btn, "COOL\nDOWN");
        lv_obj_set_style_text_align(label_stop_btn, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(label_cooldown_btn, LV_TEXT_ALIGN_CENTER, 0);
        
        lv_obj_remove_event_cb(btn_stop, end_event_cb);
        lv_obj_remove_event_cb(btn_stop, stop_resume_event_cb);
        lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_add_event_cb(btn_cooldown, cool_down_event_cb, LV_EVENT_CLICKED, NULL);

        // Restaurar estilos (fuera el rojo si lo hubiera)
        lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
        
        lv_label_set_text(ta_info, "");
    } else {
        // Estamos en pausa (STOP pulsado)
        lv_label_set_text(label_stop_btn, "RESUME");
        lv_label_set_text(label_cooldown_btn, "END");
        
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_add_event_cb(btn_cooldown, end_event_cb, LV_EVENT_CLICKED, NULL);
        
        // Estilo rojo para el botón END
        lv_obj_set_style_bg_color(btn_cooldown, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_cooldown, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label_cooldown_btn, lv_color_hex(0xFFFFFF), 0);
        
        set_info_text_persistent("Ejercicio en pausa. Pulsa RESUME para continuar o END para finalizar.");
    }
    
    lv_obj_invalidate(btn_stop);
    lv_obj_invalidate(btn_cooldown);
    update_button_states_visual();
    bsp_display_unlock();
}

void ui_cool_down(void) {
    audio_play_beep();

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (g_treadmill_state.is_stopped) {
        xSemaphoreGive(g_state_mutex);
        return;
    }

    if (!g_treadmill_state.is_cooling_down) {
        // --- 1. Bajada inicial de 1/3 ---
        float current_speed = g_treadmill_state.speed_kmh;
        g_treadmill_state.speed_before_stop = g_treadmill_state.target_speed;
        
        // Reducir 1/3 -> Queda el 66.6%
        float drop_speed = current_speed * (2.0f / 3.0f);
        // Redondear a .0 o .5 (ej: 6.6 -> 6.5, 7.3 -> 7.5? El usuario dijo "redondeando a .0 o .5")
        // Usamos roundf(x * 2) / 2 para redondear al 0.5 más cercano.
        float rounded_speed = roundf(drop_speed * 2.0f) / 2.0f;
        if (rounded_speed < 0.5f && current_speed > 0.5f) rounded_speed = 0.5f;

        g_treadmill_state.is_cooling_down = true;
        g_treadmill_state.is_resuming = false;
        g_treadmill_state.cooldown_level = 1;
        g_treadmill_state.target_speed = rounded_speed; 
        
        last_speed_ramp_update_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Aplicar bajada inmediata
        xSemaphoreGive(g_state_mutex);
        cm_master_set_speed(rounded_speed);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);

        // --- 2. Rampa de inclinación ---
        // Nivel 1: 0.5 km/h cada 15 segundos -> 0.033 km/h/s
        float time_to_stop_s = rounded_speed / (0.5f / 15.0f);
        if (time_to_stop_s > 1.0f) {
            g_treadmill_state.cooldown_climb_ramp_rate = g_treadmill_state.climb_percent / time_to_stop_s;
        } else {
            g_treadmill_state.climb_percent = 0.0f;
            g_treadmill_state.cooldown_climb_ramp_rate = 0.0f;
        }

        g_treadmill_state.ramp_mode = RAMP_MODE_COOLDOWN_STOP;
        g_treadmill_state.target_climb_percent = 0.0f; // Bajar actuador a 0% (fin de carrera)
        
        ESP_LOGI(TAG, "Cool Down iniciado. Drop: %.1f -> %.1f. Nivel 1 (30s). Actuador -> 0%%", current_speed, rounded_speed);
        
        // Aplicar bajada de inclinación inmediata (objetivo)
        xSemaphoreGive(g_state_mutex);
        cm_master_set_incline(0.0f);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    } else {
        // Ya estamos en Cool Down, subir nivel (máx 3)
        if (g_treadmill_state.cooldown_level < 3) {
            g_treadmill_state.cooldown_level++;
            ESP_LOGI(TAG, "Cool Down nivel incrementado: %d", g_treadmill_state.cooldown_level);
        }
    }

    int current_level = g_treadmill_state.cooldown_level;
    xSemaphoreGive(g_state_mutex);

    // --- ACTUALIZACIÓN DE UI ---
    bsp_display_lock(0);
    // Botón Izquierdo: RESUME
    lv_label_set_text(label_stop_btn, "RESUME");
    lv_obj_set_style_text_align(label_stop_btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_event_cb(btn_stop, stop_resume_event_cb);
    lv_obj_remove_event_cb(btn_stop, end_event_cb);
    lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Botón Derecho: COOL DOWN + o END
    if (current_level < 3) {
        lv_label_set_text(label_cooldown_btn, "COOL\nDOWN\n+");
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_add_event_cb(btn_cooldown, cool_down_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        // Nivel 3 -> Botón STOP (Idéntico al STOP normal)
        lv_label_set_text(label_cooldown_btn, "STOP");
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, stop_from_cooldown_event_cb);
        lv_obj_add_event_cb(btn_cooldown, stop_from_cooldown_event_cb, LV_EVENT_CLICKED, NULL);
        
        // Estilo normal (gris), quitar el rojo si venía de un estado anterior (aunque level 3 es nuevo)
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label_cooldown_btn, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_set_style_text_align(label_cooldown_btn, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_invalidate(btn_stop);
    lv_obj_invalidate(btn_cooldown);
    update_button_states_visual();
    bsp_display_unlock();
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
        bsp_display_lock(0);
        lv_scr_load(scr_set);
        bsp_display_unlock();
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
        bsp_display_lock(0);
        lv_scr_load(scr_set);
        bsp_display_unlock();
    }
}

static bool _handle_numpad_press_internal(char digit) {
    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT) {
        // Peso: Siempre requiere 2 dígitos
        if (g_treadmill_state.set_digit_index >= 2) return false;

        // Validar que no exceda el máximo (200)
        char temp_buffer[3];
        strncpy(temp_buffer, g_treadmill_state.set_buffer, g_treadmill_state.set_digit_index);
        temp_buffer[g_treadmill_state.set_digit_index] = digit;
        temp_buffer[g_treadmill_state.set_digit_index + 1] = '\0';

        float proposed_value = atof(temp_buffer);
        if (proposed_value > 200.0f) {
            ESP_LOGI(TAG, "Dígito inválido '%c'. El peso propuesto %.0f excede el máximo 200", digit, proposed_value);
            return false;
        }

        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = digit;
        g_treadmill_state.set_digit_index++;
        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = '\0';

        _update_set_display_text_internal();
        return (g_treadmill_state.set_digit_index >= 2);
    } 
    else if (g_treadmill_state.set_mode == SET_MODE_SPEED || g_treadmill_state.set_mode == SET_MODE_CLIMB) {
        // Velocidad e Inclinación: Lógica inteligente compartida
        // FORMATO: 0, 2-9 -> Inmediato. 1 -> Espera segundo dígito.
        
        bool is_climb = (g_treadmill_state.set_mode == SET_MODE_CLIMB);
        float max_val = is_climb ? MAX_CLIMB_PERCENT : MAX_SPEED_KMH;

        if (waiting_for_second_digit) {
            // Ya tenemos el primer dígito '1', este es el segundo (0-5 para climb, 0-9 para speed)
            if (speed_input_timeout_timer) {
                lv_timer_del(speed_input_timeout_timer);
                speed_input_timeout_timer = NULL;
            }
            waiting_for_second_digit = false;
            
            char temp_buffer[3];
            temp_buffer[0] = '1';
            temp_buffer[1] = digit;
            temp_buffer[2] = '\0';
            
            float proposed_value = atof(temp_buffer);
            if (proposed_value > max_val) {
                ESP_LOGI(TAG, "%s %.1f excede el máximo %.1f", is_climb?"Inclinacion":"Velocidad", proposed_value, max_val);
                return false;
            }
            
            strcpy(g_treadmill_state.set_buffer, temp_buffer);
            g_treadmill_state.set_digit_index = 2;
            _update_set_display_text_internal();
            return true;
            
        } else {
            // Primer dígito
            if (digit == '1') {
                // Esperar segundo dígito
                waiting_for_second_digit = true;
                first_speed_digit = '1';
                g_treadmill_state.set_buffer[0] = '1';
                g_treadmill_state.set_buffer[1] = '\0';
                g_treadmill_state.set_digit_index = 1;
                
                if (speed_input_timeout_timer) lv_timer_del(speed_input_timeout_timer);
                speed_input_timeout_timer = lv_timer_create(speed_input_timeout_cb, 5000, NULL);
                lv_timer_set_repeat_count(speed_input_timeout_timer, 1);
                
                _update_set_display_text_internal();
                return false; 
            } else {
                // Inmediato (0, 2-9)
                float val = (float)(digit - '0');
                if (val > max_val) {
                    ESP_LOGI(TAG, "%s %.1f excede el máximo %.1f", is_climb?"Inclinacion":"Velocidad", val, max_val);
                    return false;
                }
                g_treadmill_state.set_buffer[0] = digit;
                g_treadmill_state.set_buffer[1] = '\0';
                g_treadmill_state.set_digit_index = 1;
                _update_set_display_text_internal();
                return true;
            }
        }
    }
    return false;
}

bool ui_handle_numpad_press(char digit) {
    audio_play_beep();
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bsp_display_lock(0);
    bool result = _handle_numpad_press_internal(digit);
    bsp_display_unlock();
    bool should_confirm = result && (g_treadmill_state.set_mode != SET_MODE_NONE);
    xSemaphoreGive(g_state_mutex);
    
    // Si está completo Y el modo es válido, auto-confirmar
    if (should_confirm) {
        ui_confirm_set_value();
    }
    
    return result;
}

void ui_confirm_set_value(void) {
    // Evitar doble confirmación
    if (confirming_in_progress) {
        ESP_LOGI(TAG, "ui_confirm_set_value: Ya hay una confirmación en progreso, ignorando");
        return;
    }
    confirming_in_progress = true;
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);

    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT) {
        // Para peso, el valor es directo (sin división por 10)
        float weight = atof(g_treadmill_state.set_buffer);
        if (weight < 30.0f) weight = 30.0f;  // Mínimo 30 kg
        if (weight > 200.0f) weight = 200.0f; // Máximo 200 kg
        g_treadmill_state.user_weight_kg = weight;
        // Marcar que el usuario ha introducido el peso
        g_treadmill_state.weight_entered = true;

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
        float final_value = 0.0f;
        bool is_speed_mode = false;

        if (g_treadmill_state.set_mode == SET_MODE_SPEED) {
            // Velocidad: 2 dígitos enteros (ej: "14" = 14.0 km/h)
            final_value = atof(g_treadmill_state.set_buffer);
            if (final_value > MAX_SPEED_KMH) final_value = MAX_SPEED_KMH;
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
            g_treadmill_state.target_speed = final_value;
            is_speed_mode = true;
        } else if (g_treadmill_state.set_mode == SET_MODE_CLIMB) {
            // Inclinación: 2 dígitos sin dividir (ej: "05" = 5%)
            final_value = atof(g_treadmill_state.set_buffer);
            if (final_value > MAX_CLIMB_PERCENT) final_value = MAX_CLIMB_PERCENT;
            g_treadmill_state.target_climb_percent = final_value;
            is_speed_mode = false;
        } else {
            // Caso inesperado (ej: SET_MODE_NONE)
            ESP_LOGW(TAG, "ui_confirm_set_value: Modo inesperado %d, abortando", g_treadmill_state.set_mode);
            xSemaphoreGive(g_state_mutex);
            confirming_in_progress = false;
            return;
        }

        xSemaphoreGive(g_state_mutex);

        // Enviar comando al esclavo via RS485
        if (is_speed_mode) {
            ESP_LOGI(TAG, "Enviando VELOCIDAD: %.1f km/h", final_value);
            cm_master_set_speed(final_value);
        } else {
            ESP_LOGI(TAG, "Enviando INCLINACIÓN: %.1f %%", final_value);
            cm_master_set_incline(final_value);
        }

        // Actualizar inmediatamente los labels en la pantalla principal
        bsp_display_lock(0);
        if (is_speed_mode) {
            // Actualizar velocidad
            int speed_int = (int)final_value;
            int speed_frac = (int)((final_value - speed_int) * 10);
            lv_label_set_text_fmt(label_speed_kmh, "%d.%d", speed_int, speed_frac);

            // Actualizar pace (min:seg por km) - M:SS con límite 9:59
            if (final_value > 6.01f) {
                float pace_min_per_km = 60.0f / final_value;
                int pace_m = (int)pace_min_per_km;
                int pace_s = (int)((pace_min_per_km - (float)pace_m) * 60.0f + 0.5f);
                if (pace_s >= 60) {
                    pace_s = 0;
                    pace_m++;
                }
                
                if (pace_m < 10) {
                    lv_label_set_text_fmt(label_speed_pace, "%d:%02d", pace_m, pace_s);
                } else {
                    lv_label_set_text(label_speed_pace, "-:--");
                }
            } else {
                lv_label_set_text(label_speed_pace, "-:--");
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
    
    // Limpiar bandera de confirmación
    confirming_in_progress = false;
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

    // ACTIVAR TRAINING MODE (entrando a pantalla principal después de descarga)
    cm_master_set_training_mode(true);
    ESP_LOGI(TAG, "Entrando a pantalla principal - Training mode activado");
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

static void manual_up_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        audio_play_beep();
        cm_master_manual_incline_up();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        cm_master_manual_incline_stop();
    }
}

static void manual_down_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        audio_play_beep();
        cm_master_manual_incline_down();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        cm_master_manual_incline_stop();
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
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        bool is_stopped = g_treadmill_state.is_stopped;
        int cd_level = g_treadmill_state.cooldown_level;
        xSemaphoreGive(g_state_mutex);

        // Si estamos en STOP (Pausa), el botón derecho es END
        if (is_stopped) {
            ESP_LOGI(TAG, "ui_weight_entry: llamando ui_finish_training() (is_stopped=true)");
            audio_play_beep();
            ui_finish_training();
        } else if (cd_level >= 3) {
            // En Nivel 3 el botón derecho es STOP (Pausar cinta)
            ESP_LOGI(TAG, "ui_weight_entry: llamando ui_stop_from_cooldown() (level=%d)", cd_level);
            ui_stop_from_cooldown();
        } else {
            ESP_LOGI(TAG, "ui_weight_entry: llamando ui_cool_down()");
            ui_cool_down();
        }
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
            cm_master_set_training_mode(true);  // ACTIVAR TRAINING MODE
            ESP_LOGI(TAG, "Training mode activado (botón físico)");
            set_info_text_persistent("Selecciona una velocidad para comenzar");
            bsp_display_unlock();
            break;
        case 2:
            ESP_LOGI(TAG, "Entrenamiento Itsaso seleccionado (botón físico) - iniciando descarga IA");
            bsp_display_lock(0);
            lv_scr_load(scr_loading);  // Pantalla negra durante descarga
            bsp_display_unlock();
            ia_sync_get_next_plan("Itsaso", on_plan_received);
            break;
        case 3:
            ESP_LOGI(TAG, "Entrenamiento Ina seleccionado (botón físico) - iniciando descarga IA");
            bsp_display_lock(0);
            lv_scr_load(scr_loading);  // Pantalla negra durante descarga
            bsp_display_unlock();
            ia_sync_get_next_plan("Ina", on_plan_received);
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