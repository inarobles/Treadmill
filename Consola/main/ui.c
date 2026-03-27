#include "ui.h"
#ifndef SIMULATOR
#include "audio.h"
#include "wifi_client.h"
#include "wifi_manager.h"
#endif
#include "treadmill_state.h"
#include "ble_client.h"
#include "ble_ftms.h"
#include "cm_master.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "esp_timer.h"
#ifndef SIMULATOR
#include "ia_telemetry.h"
#endif
#include "ia_sync.h"
#include "freertos/task.h"
#ifndef SIMULATOR
#include "bsp/esp32_p4_function_ev_board.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "acoustic_service.h"
#else
// Stubs para el SIMULADOR (evitar errores de declaración implícita y tipos faltantes)
extern void audio_set_volume(int volume);
extern void audio_play_beep(void);
extern void acoustic_service_set_current_speed(float speed);
extern void acoustic_service_init(void);
extern uint32_t acoustic_service_get_steps(void);
extern float acoustic_service_get_cadence(void);
extern bool acoustic_service_is_calibrating(void);
extern void acoustic_service_start_auto_calibration(void);
extern float acoustic_service_get_measured_noise(void);
extern void acoustic_service_set_speed_threshold(uint8_t kmh, float threshold);
extern void acoustic_service_dump_samples(void);
extern void bsp_display_brightness_set(int brightness);
extern void ia_telemetry_stop_session(void);
extern const char* ia_telemetry_get_current_report(void);
extern bool is_internet_connected(void);
extern bool is_wifi_connected(void);
extern esp_err_t wifi_manager_get_current_ssid(char *ssid);
extern void bsp_display_lock(uint32_t timeout_ms);
extern void bsp_display_unlock(void);
#endif


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
const float STOP_RAMP_RATE_KMH_S = 5.0f;    // Rampa rÃ¡pida para detener/reanudar
const float COOLDOWN_RESUME_RAMP_RATE_KMH_S = 0.1f; // 0.1 km/h cada segundo para reanudar

static uint32_t last_speed_ramp_update_ms = 0;

//==================================================================================
// 1B. FUNCIONES DE PERSISTENCIA (NVS)
//==================================================================================

#define NVS_NAMESPACE_WAX "wax_maintenance"
#define NVS_KEY_TOTAL_SECONDS "total_seconds"
#define NVS_KEY_LAST_WAX_TIME "last_wax_time"
#define NVS_KEY_WAX_DIST      "wax_dist"


#define NVS_NAMESPACE_SETTINGS "app_settings"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_VOLUME "volume"
#define NVS_KEY_SENSITIVITY "sensitivity"


/**
 * @brief Carga el contador de horas de cera desde NVS
 * @return Segundos acumulados, o 0 si no hay datos guardados
 */
static uint32_t load_wax_counter_from_nvs(void) {
#ifndef SIMULATOR
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_WAX, NVS_READONLY, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error opening NVS for wax counter read: %s", esp_err_to_name(err));
        return 0;
    }

    uint32_t total_seconds = 0;
    nvs_get_u32(nvs_handle, NVS_KEY_TOTAL_SECONDS, &total_seconds);
    
    uint32_t last_time = 0;
    nvs_get_u32(nvs_handle, NVS_KEY_LAST_WAX_TIME, &last_time);
    g_treadmill_state.last_wax_timestamp = last_time;

    uint32_t dist_raw = 0;
    nvs_get_u32(nvs_handle, NVS_KEY_WAX_DIST, &dist_raw);
    g_treadmill_state.total_running_distance_wax_km = (double)dist_raw / 1000.0;

    nvs_close(nvs_handle);
    return total_seconds;
#else
    return 0;
#endif
}

/**
 * @brief Guarda el contador de horas de cera en NVS
 * @param total_seconds Segundos acumulados a guardar
 */
static void save_wax_counter_to_nvs(uint32_t total_seconds) {
#ifndef SIMULATOR
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_WAX, NVS_READWRITE, &nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS for wax counter write: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_u32(nvs_handle, NVS_KEY_TOTAL_SECONDS, total_seconds);
    nvs_set_u32(nvs_handle, NVS_KEY_LAST_WAX_TIME, g_treadmill_state.last_wax_timestamp);
    nvs_set_u32(nvs_handle, NVS_KEY_WAX_DIST, (uint32_t)(g_treadmill_state.total_running_distance_wax_km * 1000.0));
    
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
#endif
}

static void load_sensitivity_from_nvs(uint8_t *sens) {
    *sens = 50; // Default
#ifndef SIMULATOR
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE_SETTINGS, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, NVS_KEY_SENSITIVITY, sens);
        nvs_close(nvs_handle);
    }
#endif
}





//==================================================================================
// 2. PUNTEROS GLOBALES A OBJETOS LVGL
//==================================================================================
// -- Pantalla de SelecciÃ³n de Entrenamiento --
lv_obj_t *scr_training_select;  // Accesible desde ui_wifi.c
static lv_obj_t *btn_training_itsaso;
static lv_obj_t *label_training_itsaso;
static lv_obj_t *btn_training_ina;
static lv_obj_t *label_training_ina;
static lv_timer_t *wifi_check_timer;

typedef struct {
    int num_vueltas;
    struct {
        float speed;
        int climb;
        int duration_secs;
        bool steps_enabled;
        int spm;
        bool sound_enabled;
    } work;
    struct {
        float speed;
        int climb;
        int duration_secs;
        bool steps_enabled;
        int spm;
        bool sound_enabled;
    } rest;
} series_config_t;

static lv_obj_t *scr_series_config;
static series_config_t g_series_config;
static lv_obj_t *label_series_count;
static lv_obj_t *label_series_work_speed, *label_series_work_climb, *label_series_work_time;
static lv_obj_t *label_series_work_steps, *label_series_work_spm, *label_series_work_sound;
static lv_obj_t *btn_series_work_steps, *btn_series_work_sound;
static lv_obj_t *label_series_rest_speed, *label_series_rest_climb, *label_series_rest_time;
static lv_obj_t *label_series_rest_steps, *label_series_rest_spm, *label_series_rest_sound;
static lv_obj_t *btn_series_rest_steps, *btn_series_rest_sound;

// -- Calibration State Machine --
typedef enum {
    CAL_IDLE,
    CAL_WAITING_USER_OFF,
    CAL_COUNTDOWN,
    CAL_RAMP_UP,
    CAL_STABILIZING,
    CAL_MEASURING,
    CAL_NEXT_STEP,
    CAL_RAMP_DOWN,
    CAL_FINISHED
} cal_seq_state_t;

static cal_seq_state_t s_cal_seq = CAL_IDLE;
static int s_cal_current_kmh = 0;
static uint32_t s_cal_state_timer = 0;
#define CAL_STABILIZE_TIME_MS      4000
#define CAL_MEASURE_TIME_MS        5000

// -- Pantalla de escaneo BLE --
static lv_obj_t *scr_ble_scan;
static lv_obj_t *list_ble_devices;
static lv_obj_t *spinner_ble_scan;

// -- Pantalla de Carga --
static lv_obj_t *scr_loading;

// -- Pantalla de Apagado --
static lv_obj_t *scr_shutdown;

// -- Pantalla de Subida --
static lv_obj_t *scr_uploading;

// -- Pantalla de Calibracion Podometro --
static lv_obj_t *scr_calibration;
static lv_obj_t *label_cal_speed;
static lv_obj_t *label_cal_status;
static lv_obj_t *bar_cal_progress;

// -- Pantalla Principal --
static lv_obj_t *scr_main;
static lv_obj_t *label_dist;
static lv_obj_t *label_time;
static lv_obj_t *label_climb_percent;
static lv_obj_t *label_speed_kmh;
static lv_obj_t *label_speed_pace;
static lv_obj_t *label_pulse;
static lv_obj_t *label_stride;
static lv_obj_t *label_kcal;
static lv_obj_t *unit_kcal_main;  // Label de unidad "Kcal" en pantalla MAIN
static lv_obj_t *unit_kcal_set;  // Label de unidad "Kcal" en pantalla SET
static lv_obj_t *label_stop_btn;
static lv_obj_t *label_cooldown_btn;
static lv_obj_t *btn_stop;
static lv_obj_t *btn_cooldown;
static lv_obj_t *btn_upload_training;
static lv_obj_t *ta_info;
static lv_obj_t *label_status_wifi;
static lv_obj_t *label_status_ble;
static lv_obj_t *label_status_wax;

LV_IMG_DECLARE(icon_main);

static ia_plan_t g_current_plan;
// Botones de velocidad e inclinaciÃ³n (para deshabilitaciÃ³n visual)
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
static bool buttons_are_stop_mode = false;
static bool showing_weight_in_kcal_field = false;

// Variables para lÃ³gica inteligente de entrada de velocidad
static bool waiting_for_second_digit = false;
static lv_timer_t *speed_input_timeout_timer = NULL;
static char first_speed_digit = '\0';
static bool confirming_in_progress = false;

// -- Pantalla de Ajuste (Clon) --
static lv_obj_t *scr_set;
static lv_obj_t *scr_wax;
static lv_obj_t *label_wax_days;
static lv_obj_t *label_wax_usage_time;
static lv_obj_t *label_wax_dist;
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
static lv_obj_t *stride_circle; // Generic pointer, used for creation
static lv_obj_t *stride_circle_main; // Specific pointer for Main Screen circle (for Step Control)
static lv_obj_t *label_1000m; // Reference to update distance limit label
static lv_obj_t *label_mid_dist; // Reference to update middle distance label
static lv_obj_t *label_q1_dist;  // Quarter 1 (25%)
static lv_obj_t *label_q3_dist;  // Quarter 3 (75%)

// --- Dynamic Power Profile Variables ---
#define POWER_HISTORY_SIZE 100
static float power_history[POWER_HISTORY_SIZE];
static lv_obj_t *power_dots[POWER_HISTORY_SIZE];
static lv_point_t power_points[POWER_HISTORY_SIZE];
static lv_obj_t *power_line;
static lv_obj_t *v_line_mid;    // Reference to update middle line
static lv_obj_t *v_line_q1;     // Reference for quarter 1 line
static lv_obj_t *v_line_q3;     // Reference for quarter 3 line
static float max_power_ref = 300.0f;
static float dist_resolution_m = 10.0f;
static int current_power_index = 0;
static double last_dist_ref_km = 0;

// --- Overlay Numpad Variables (for Series Configuration) ---
static void create_overlay_numpad(lv_obj_t *parent);
static void _show_overlay_numpad(set_mode_t mode);
static lv_obj_t *overlay_numpad_container;
static lv_obj_t *overlay_numpad_bg; // Semi-transparent background
static lv_obj_t *label_overlay_numpad_value;
static lv_obj_t *label_overlay_numpad_title;
static lv_obj_t *label_overlay_numpad_unit;
static char overlay_numpad_buffer[8] = {0}; 
static set_mode_t overlay_numpad_mode = SET_MODE_NONE;

// --- Step Control Logic ---
static int step_control_target_spm = 0; // Steps Per Minute
static bool step_control_audio_enabled = false;
static lv_timer_t *step_control_timer = NULL;
static bool step_control_visual_state = false; // true = green, false = transparent

// --- Step Control Logic ---
static void step_control_timer_cb(lv_timer_t *timer) {
    if (!stride_circle_main) return; // Only animate the Main Screen circle

    // Only enable visual/audio feedback if the treadmill is moving
    if (g_treadmill_state.speed_kmh <= 0.05f) {
        lv_obj_set_style_bg_opa(stride_circle_main, LV_OPA_TRANSP, 0);
        step_control_visual_state = false;
        return;
    }

    step_control_visual_state = !step_control_visual_state;

    if (step_control_visual_state) {
        // Flash Green
        lv_obj_set_style_bg_color(stride_circle_main, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_bg_opa(stride_circle_main, LV_OPA_COVER, 0);
        
        if (step_control_audio_enabled) {
            audio_play_beep();
        }
    } else {
        // Transparent (off)
        lv_obj_set_style_bg_opa(stride_circle_main, LV_OPA_TRANSP, 0);
    }
}

static void update_step_control_timer(void) {
    if (step_control_timer) {
        lv_timer_del(step_control_timer);
        step_control_timer = NULL;
    }

    if (step_control_target_spm > 0) {
        // Calculate interval in ms for half-cycle (state toggle)
        // SPM = Steps Per Minute.
        // Beat interval = 60000 / SPM (ms)
        // We want 1 flash per beat.
        // Options:
        // A) Variable duty cycle: Short flash (e.g., 100ms) then off for rest of beat.
        // B) 50% duty cycle: Toggle every (60000 / SPM) / 2.
        
        // Let's implement Option A for a sharper "metronome" feel.
        // Actually, timer_cb is just called periodically.
        // If we want it to flash ON exactly at the beat start, we can set period = Beat Interval.
        // And use a separate mechanism or a one-shot timer to turn it off?
        // Or simpler: Toggle mode with very short ON time?
        
        // Let's stick to a simple Blink for now: Toggle state. 
        // If we toggle every Beat/2, it's 50% duty cycle.
        uint32_t interval = (60000 / step_control_target_spm) / 2;
        step_control_timer = lv_timer_create(step_control_timer_cb, interval, NULL);
    } else {
        // Reset visual state if stopped
        if (stride_circle_main) {
           lv_obj_set_style_bg_opa(stride_circle_main, LV_OPA_TRANSP, 0);
        }
    }
}
static float power_accumulator = 0;
static int power_sample_count = 0;
static lv_obj_t *ta_info_set;

// -- Pantalla de Ajustes APP --
static lv_obj_t *scr_app_settings;
static lv_obj_t *arc_brightness;
static lv_obj_t *arc_volume;
static lv_obj_t *label_brightness_pct;
static lv_obj_t *label_volume_pct;
static lv_obj_t *slider_sensitivity;
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
static void create_series_config_screen(void);
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
static void wax_4kmh_event_cb(lv_event_t *e);
static void wax_stop_event_cb(lv_event_t *e);
static void wax_back_event_cb(lv_event_t *e);
static void on_report_sent(bool success, const char *error_msg);
static void ui_stop_from_cooldown(void);

// --- IA Sync Callbacks ---
static void on_plan_received(const ia_plan_t *plan, const char *error_msg) {
    bsp_display_lock(portMAX_DELAY);
    if (plan) {
        ESP_LOGI(TAG, "Plan recibido: %s con %d bloques", plan->plan_id, plan->block_count);
        
        // Initialize execution state
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
        ESP_LOGE(TAG, "Error al recibir plan: %s", error_msg ? error_msg : "Desconocido");
        set_info_text_persistent("Error al descargar plan. Intentalo de nuevo.");
        lv_scr_load(scr_training_select);
    }
    bsp_display_unlock();
}



static void on_report_sent(bool success, const char *error_msg) {
    bsp_display_lock(portMAX_DELAY);
    if (success) {
        ESP_LOGI(TAG, "Reporte enviado con Ã©xito");
        set_info_text_persistent("Â¡Entrenamiento guardado! Buen trabajo.");
        
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
// 4. TAREA PRINCIPAL DE ACTUALIZACIÃ“N
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
        if (btn_training_ina && label_training_ina) {
            lv_obj_add_flag(btn_training_ina, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_color(label_training_ina, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(label_training_ina, "Descargar Entreno");
        }
    } else {
        if (btn_training_ina && label_training_ina) {
            lv_obj_clear_flag(btn_training_ina, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_text_color(label_training_ina, lv_color_hex(0x000000), 0);
            lv_label_set_text(label_training_ina, "Conectando...");
        }
    }
    
    // El botón Itsaso (Definir Entreno) ya no depende de internet, es local.
    if (btn_training_itsaso && label_training_itsaso) {
        lv_obj_add_flag(btn_training_itsaso, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(label_training_itsaso, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(label_training_itsaso, "Definir Entreno");
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
    
    // 4. Parpadeo del Aviso WAX (basado en umbrales)
    if (label_status_wax) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        uint32_t total_seconds = g_treadmill_state.total_running_seconds;
        double total_km = g_treadmill_state.total_running_distance_wax_km;
        uint32_t last_wax = g_treadmill_state.last_wax_timestamp;
        xSemaphoreGive(g_state_mutex);

        bool exceeded = false;
        
        // Umbral 1: Tiempo de uso >= 150 horas (150 * 3600 = 540000 segundos)
        if (total_seconds >= 540000) exceeded = true;
        
        // Umbral 2: Distancia >= 2000 km
        if (total_km >= 2000.0) exceeded = true;

        // Umbral 3: Dias naturales >= 60
        if (!exceeded && last_wax != 0) {
            time_t now = time(NULL);
            int diff_days = (int)(difftime(now, (time_t)last_wax) / (60 * 60 * 24));
            if (diff_days >= 60) exceeded = true;
        }

        if (exceeded) {
            static uint32_t wax_blink_counter = 0;
            wax_blink_counter++;
            if (wax_blink_counter >= 5) { // Cada 500ms (100ms * 5)
                wax_blink_counter = 0;
                if (lv_obj_has_flag(label_status_wax, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_clear_flag(label_status_wax, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(label_status_wax, LV_OBJ_FLAG_HIDDEN);
                }
            }
        } else {
            lv_obj_add_flag(label_status_wax, LV_OBJ_FLAG_HIDDEN);
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

// Power Filtering State
static float last_valid_power = 0.0f;
static bool power_filter_initialized = false;

// ... (existing code)

void ui_update_task(void *pvParameter) {
    const uint32_t UI_UPDATE_INTERVAL_MS = 100;
#ifndef SIMULATOR
    acoustic_service_init(); 
#endif

    // High resolution timer state
    int64_t last_time_us = esp_timer_get_time();
    double accumulated_seconds_fraction = 0.0;

    static bool was_stopped = true;
    static int prev_speed_int = -1, prev_speed_frac = -1, prev_climb_int = -1;
    static uint32_t prev_elapsed_seconds = 0xFFFFFFFF;
    static int prev_dist_value = -1;
    static int prev_pace_int = -1, prev_pace_frac = -1;
    static int prev_pulse = -1;
    static int prev_steps = -1, prev_kcal = -1;
    static bool need_restore_weight_buttons = false;
    static uint32_t heartbeat_counter = 0;
    static uint32_t ftms_notify_counter = 0;  // Counter for FTMS BLE speed notifications

    while (1) {
        heartbeat_counter++;
        if (heartbeat_counter >= 10) {
            ESP_LOGD(TAG, "UI Heartbeat (Task alive)");
            heartbeat_counter = 0;
        }

        // Calculate exact delta time
        int64_t now_us = esp_timer_get_time();
        float dt_seconds = (float)(now_us - last_time_us) / 1000000.0f;
        last_time_us = now_us;

        // Sanity check for dt (to avoid huge jumps after pause/debug)
        if (dt_seconds > 1.0f) dt_seconds = 1.0f; 
        
        uint32_t now = (uint32_t)(now_us / 1000); // Keep existing MS logic for timers

        char cal_status_text[64] = "";
        bool update_cal_ui = false;
        bool trigger_locked_msg = false;
        bool trigger_recovered_msg = false;
        bool trigger_reset_power_ui = false;
        bool update_power_chart = false;
        float avg_power_to_render = 0;

        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        
        // 1. HARDWARE POLL
#ifndef SIMULATOR
        if (!acoustic_service_is_calibrating()) {
             g_treadmill_state.steps = acoustic_service_get_steps();
             g_treadmill_state.cadence = (uint32_t)acoustic_service_get_cadence();
        }
        acoustic_service_set_current_speed(g_treadmill_state.speed_kmh);
#endif

        float real_speed_from_slave = cm_master_get_real_speed();
        float real_incline_from_slave = cm_master_get_current_incline();
        g_treadmill_state.speed_kmh = real_speed_from_slave;
        g_treadmill_state.climb_percent = real_incline_from_slave;

        head_value = cm_master_get_head_fan_state();
        chest_value = cm_master_get_chest_fan_state();
        bool incline_sensor_fault_active = cm_master_get_incline_sensor_fault();

        // 1.1 SIMULATOR LOGIC (Internal state updates)
#ifdef SIMULATOR
        if (lv_scr_act() == scr_main) { 
            // Simulator distance update (simplified)
            g_treadmill_state.total_distance_km += (g_treadmill_state.speed_kmh / 3600.0f) * dt_seconds;
            accumulated_seconds_fraction += dt_seconds;
            if (accumulated_seconds_fraction >= 1.0) {
                g_treadmill_state.elapsed_seconds += 1;
                accumulated_seconds_fraction -= 1.0;
            }
            g_treadmill_state.speed_kmh = 12.0f + 6.0f * sinf(g_treadmill_state.elapsed_seconds * 0.1f);
            g_treadmill_state.climb_percent = 3.0f + 3.0f * cosf(g_treadmill_state.elapsed_seconds * 0.05f);
            g_treadmill_state.weight_entered = true; 
            if (g_treadmill_state.user_weight_kg < 1.0f) g_treadmill_state.user_weight_kg = 75.0f;
        }
#endif

        // 2. SAFETY & LOCKS LOGIC
        static bool sys_locked = false;
        if (incline_sensor_fault_active && !sys_locked) {
            sys_locked = true;
            g_treadmill_state.target_speed = 0.0f;
            cm_master_set_speed(0.0f);
            trigger_locked_msg = true;
        } else if (!incline_sensor_fault_active && sys_locked) {
            sys_locked = false;
            trigger_recovered_msg = true;
        }

        if (sys_locked) {
            g_treadmill_state.target_speed = 0.0f;
            g_treadmill_state.speed_kmh = 0.0f;
            xSemaphoreGive(g_state_mutex);
            vTaskDelay(pdMS_TO_TICKS(UI_UPDATE_INTERVAL_MS));
            
            // Still update timer even if locked to avoid jump on resume
            last_time_us = esp_timer_get_time();
            continue;
        }

        // 3. MOTION & MATH LOGIC
        if (g_treadmill_state.ramp_mode == RAMP_MODE_COOLDOWN_STOP) {
            uint32_t interval_ms = (g_treadmill_state.cooldown_level == 2) ? 15000 : (g_treadmill_state.cooldown_level == 3 ? 5000 : 30000);
            if (now - last_speed_ramp_update_ms >= interval_ms) {
                last_speed_ramp_update_ms = now;
                float cur = g_treadmill_state.speed_kmh;
                if (cur > 0.5f) cm_master_set_speed(roundf((cur - 0.5f) * 2.0f) / 2.0f);
                else cm_master_set_speed(0.0f);
            }
        } 
        else if (g_treadmill_state.is_resuming) {
            // AceleraciÃ³n RESUME: El usuario quiere que la gestione el variador (rampa hardware),
            // eliminando la rampa por software (+0.5 km/h por segundo).
            float target = g_treadmill_state.target_speed;
            cm_master_set_speed(target);
            g_treadmill_state.is_resuming = false;
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
            ESP_LOGI(TAG, "AceleraciÃ³n RESUME enviada directamente al variador: %.2f km/h", target);
        }

        // 4. ACCUMULATORS
        if (g_treadmill_state.speed_kmh > 0.0f) {
            if (was_stopped) { was_stopped = false; need_restore_weight_buttons = true; }
            
            // Time Accumulation (precise)
            accumulated_seconds_fraction += dt_seconds;
            if (accumulated_seconds_fraction >= 1.0) {
                accumulated_seconds_fraction -= 1.0;
                
                g_treadmill_state.elapsed_seconds++;
                g_treadmill_state.total_running_seconds++;
                static uint32_t nvs_save_counter = 0;
                if (++nvs_save_counter >= 60) { nvs_save_counter = 0; save_wax_counter_to_nvs(g_treadmill_state.total_running_seconds); }
            }
            
            // Distance Accumulation (precise based on real speed)
            double dist_interval = (double)g_treadmill_state.speed_kmh / 3600.0 * dt_seconds;
            g_treadmill_state.total_distance_km += dist_interval;
            g_treadmill_state.total_running_distance_wax_km += dist_interval;

            // --- EJECUCION DE PLAN IA / SERIES ---
            if (g_treadmill_state.plan_running) {
                static double plan_tick_accumulator = 0;
                plan_tick_accumulator += dt_seconds;
                if (plan_tick_accumulator >= 1.0) {
                    plan_tick_accumulator -= 1.0;
                    g_treadmill_state.block_elapsed_seconds++;
                    
                    ia_block_t *cur_block = &g_current_plan.blocks[g_treadmill_state.current_block_idx];
                    bool finished = false;
                    if (cur_block->primary_cond_type == IA_CONDITION_TIME) {
                        if (g_treadmill_state.block_elapsed_seconds >= (uint32_t)cur_block->primary_cond_value) finished = true;
                    }
                    if (!finished && cur_block->secondary_cond_s > 0) {
                        if (g_treadmill_state.block_elapsed_seconds >= cur_block->secondary_cond_s) finished = true;
                    }

                    if (finished) {
                        g_treadmill_state.current_block_idx++;
                        if (g_treadmill_state.current_block_idx < g_current_plan.block_count) {
                            g_treadmill_state.block_elapsed_seconds = 0;
                            ia_block_t *next = &g_current_plan.blocks[g_treadmill_state.current_block_idx];
                            
                            // Aplicar nuevos objetivos
                            g_treadmill_state.target_speed = next->target_speed;
                            g_treadmill_state.target_climb_percent = next->target_incline;
                            cm_master_set_speed(next->target_speed);
                            cm_master_set_incline(next->target_incline);

                            // Sincronizar Step Control
                            step_control_target_spm = (next->steps_enabled) ? next->spm : 0;
                            step_control_audio_enabled = next->sound_enabled;
                            update_step_control_timer();

                            // Actualizar info en pantalla
                            char info_buf[128];
                            snprintf(info_buf, sizeof(info_buf), "%s\n%s", next->tramo_label, next->bloque_label);
                            lv_label_set_text(ta_info, info_buf);
                            
                            ESP_LOGI(TAG, "Plan: switch to block %d (%s)", g_treadmill_state.current_block_idx, next->bloque_label);
                        } else {
                            g_treadmill_state.plan_running = false;
                            set_info_text("Â¡Entrenamiento finalizado!");
                        }
                    }
                }
            }

            // Power Metrics with Filtering
            float weight_kg = g_treadmill_state.weight_entered ? g_treadmill_state.user_weight_kg : 75.0f;
            float speed_m_min = g_treadmill_state.speed_kmh * 1000.0f / 60.0f;
            float slope_decimal = g_treadmill_state.climb_percent / 100.0f;
            float vo2 = (0.2f * speed_m_min) + (0.9f * speed_m_min * slope_decimal) + 3.5f;
            float current_power_watts = (vo2 * weight_kg / 1000.0f * 5.0f) * 69.73f * 0.25f;
            
            // --- POWER SPIKE FILTERING ---
            // If the jump is unrealistic (> 500W change in 0.1s is likely noise unless starting)
            // But we allow it to stabilize initially.
            if (!power_filter_initialized) {
                last_valid_power = current_power_watts;
                power_filter_initialized = true;
            } else {
                float power_delta = current_power_watts - last_valid_power;
                const float MAX_POWER_JUMP = 500.0f; 
                if (fabsf(power_delta) > MAX_POWER_JUMP) {
                    current_power_watts = last_valid_power + (power_delta > 0 ? 10.0f : -10.0f);
                    ESP_LOGD(TAG, "Power spike: clamping");
                }
                last_valid_power = current_power_watts;
            }
            
            power_accumulator += current_power_watts;
            power_sample_count++;

            // --- SUMMARY ACCUMULATORS ---
            // Energy (Joules) = Power (W) * Time (s)
            g_treadmill_state.accumulated_energy_joules += current_power_watts * dt_seconds;

            // Heartbeats = (BPM / 60) * dt
            float hr = (float)(g_treadmill_state.ble_connected ? g_treadmill_state.real_pulse : g_treadmill_state.sim_pulse);
            g_treadmill_state.accumulated_heartbeats += (hr / 60.0f) * dt_seconds;

            // Elevation Gain (m) = Distance (m) * sin(slope)
            // slope % = tan(angle) * 100 -> angle = atan(slope/100)
            // sin(angle) approx tan(angle) for small angles, but let's be precise:
            // Elev gain = distance_km * 1000 * (climb_percent / 100.0) -> This is "rise" based on horizontal run approx.
            // On a treadmill, grade usually means Rise / Run. So 10% = 10m rise per 100m horizontal.
            // Distance measured is usually belt length (hypotenuse), but for gym treadmills dist ~ horizontal.
            // Let's use standard approximation: Rise = Dist * (Percent/100).
            if (g_treadmill_state.climb_percent > 0.0f) {
                 double dist_m = dist_interval * 1000.0;
                 double elev_m = dist_m * (g_treadmill_state.climb_percent / 100.0);
                 if (elev_m > 0) g_treadmill_state.accumulated_elevation_gain_m += elev_m;
            }

            double dist_delta_km = g_treadmill_state.total_distance_km - last_dist_ref_km;
            if (dist_delta_km >= (dist_resolution_m / 1000.0)) {
                avg_power_to_render = power_accumulator / (float)power_sample_count;
                update_power_chart = true;
                power_accumulator = 0;
                power_sample_count = 0;
                last_dist_ref_km = g_treadmill_state.total_distance_km;
            }

            if (g_treadmill_state.weight_entered) {
                float kcal = (vo2 * g_treadmill_state.user_weight_kg * (dt_seconds/60.0f)) / 200.0f;
                g_treadmill_state.sim_kcal += kcal;
            }

            // Send FTMS BLE speed notification every ~1 second (10 × 100ms)
            ftms_notify_counter++;
            if (ftms_notify_counter >= 10) {
                ftms_notify_counter = 0;
                ble_ftms_update_speed(g_treadmill_state.speed_kmh);
            }
        } else { 
            // --- STOPPED STATELOGIC ---
            if (!was_stopped) {
                 // TRANSITION TO STOP
                 was_stopped = true;
                 power_filter_initialized = false;
                 ftms_notify_counter = 0;
                 ble_ftms_update_speed(0.0f);  // Notify FTMS clients that speed is now 0
                 
                 // Show Summary ONLY IF we have significant data (>100m or >1 min) to avoid glitches on startup
                 if (g_treadmill_state.total_distance_km > 0.05 || g_treadmill_state.total_running_seconds > 30) {
                     char summary_buf[256];
                     
                     // Calculate Averages
                     double total_time_h = g_treadmill_state.total_running_seconds / 3600.0;
                     double avg_power = (total_time_h > 0) ? (g_treadmill_state.accumulated_energy_joules / g_treadmill_state.total_running_seconds) : 0;
                     double avg_hr = (total_time_h > 0) ? (g_treadmill_state.accumulated_heartbeats / (total_time_h * 60.0)) : 0;
                     double eff_beats_km = (g_treadmill_state.total_distance_km > 0) ? (g_treadmill_state.accumulated_heartbeats / g_treadmill_state.total_distance_km) : 0;
                     int work_kj = (int)(g_treadmill_state.accumulated_energy_joules / 1000.0);
                     
                     uint32_t h = g_treadmill_state.total_running_seconds / 3600; 
                     uint32_t m = (g_treadmill_state.total_running_seconds % 3600) / 60; 
                     uint32_t s = g_treadmill_state.total_running_seconds % 60;

                     snprintf(summary_buf, sizeof(summary_buf), 
                         "RESUMEN:\n"
                         "D:%.2fkm T:%u:%02u:%02u\n"
                         "Kcal:%d Trb:%dkJ\n"
                         "Pot:%.0fW Elv:+%.0fm\n"
                         "HR:%.0f Efic:%.0fb/km",
                         g_treadmill_state.total_distance_km, h, m, s,
                         (int)g_treadmill_state.sim_kcal, work_kj,
                         avg_power, g_treadmill_state.accumulated_elevation_gain_m,
                         avg_hr, eff_beats_km
                     );
                     
                     set_info_text_persistent(summary_buf);
                 }
            }
            accumulated_seconds_fraction = 0;
        }

        // 5. CALIBRATION SEQUENCER LOGIC
        bool cal_load_settings = false;
        if (s_cal_seq != CAL_IDLE) {
            update_cal_ui = true;
            switch (s_cal_seq) {
                case CAL_WAITING_USER_OFF:
                    strcpy(cal_status_text, "BAJESE DE LA CINTA Y ESPERE...");
                    s_cal_state_timer = now;
                    s_cal_seq = CAL_COUNTDOWN;
                    break;
                case CAL_COUNTDOWN:
                    if (now - s_cal_state_timer > 5000) { s_cal_current_kmh = 4; s_cal_seq = CAL_RAMP_UP; }
                    else snprintf(cal_status_text, sizeof(cal_status_text), "INICIANDO EN %d SEG...", 5 - (int)((now - s_cal_state_timer)/1000));
                    break;
                case CAL_RAMP_UP:
                    cm_master_set_speed((float)s_cal_current_kmh);
                    s_cal_state_timer = now;
                    s_cal_seq = CAL_STABILIZING;
                    break;
                case CAL_STABILIZING:
                    strcpy(cal_status_text, "ESTABILIZANDO MOTOR...");
                    if (now - s_cal_state_timer > CAL_STABILIZE_TIME_MS) {
#ifndef SIMULATOR
                        acoustic_service_start_auto_calibration();
#endif
                        s_cal_state_timer = now;
                        s_cal_seq = CAL_MEASURING;
                    }
                    break;
                case CAL_MEASURING:
                    strcpy(cal_status_text, "MIDIENDO RUIDO ESTRUCTURAL...");
#ifndef SIMULATOR
                    if (!acoustic_service_is_calibrating() || (now - s_cal_state_timer > 15000)) {
                        if (!acoustic_service_is_calibrating()) {
                            float noise = acoustic_service_get_measured_noise();
                            // Enviamos el ruido base (max deviation). El servicio aplicarÃ¡ el offset de seguridad (+0.12)
                            acoustic_service_set_speed_threshold((uint8_t)s_cal_current_kmh, noise);
                        } else acoustic_service_set_speed_threshold((uint8_t)s_cal_current_kmh, 0.050f);
                        s_cal_seq = CAL_NEXT_STEP;
                    }
#else
                    s_cal_seq = CAL_NEXT_STEP; // En el simulador saltamos rÃ¡pido
#endif
                    break;
                case CAL_NEXT_STEP:
                    s_cal_current_kmh++;
                    s_cal_seq = (s_cal_current_kmh > 19) ? CAL_RAMP_DOWN : CAL_RAMP_UP;
                    break;
                case CAL_RAMP_DOWN:
                    strcpy(cal_status_text, "FINALIZANDO... FRENANDO");
                    cm_master_set_speed(0.0f);
                    if (cm_master_get_real_speed() < 0.2f) { s_cal_state_timer = now; s_cal_seq = CAL_FINISHED; }
                    break;
                case CAL_FINISHED:
                    strcpy(cal_status_text, "CALIBRACION COMPLETADA");
                    if (now - s_cal_state_timer > 2000) { s_cal_seq = CAL_IDLE; cal_load_settings = true; }
                    break;
                default: break;
            }
        }

        // 6. SNAPSHOT & UNLOCK
        TreadmillState snapshot = g_treadmill_state;
        uint8_t h_f_snap = head_value, c_f_snap = chest_value;
        xSemaphoreGive(g_state_mutex);

        // 7. RENDER PHASE
        bsp_display_lock(portMAX_DELAY);

        if (trigger_locked_msg) set_info_text_persistent("ERROR CRITICO: Sensor de inclinacion averiado.");
        if (trigger_recovered_msg) set_info_text("Sistema recuperado.");
        if (trigger_locked_msg || trigger_recovered_msg) {
            lv_obj_t *btns[] = {btn_speed_inc, btn_speed_dec, btn_speed_set, btn_climb_inc, btn_climb_dec, btn_climb_set, btn_stop, btn_cooldown};
            for(int i=0; i<8; i++) trigger_locked_msg ? lv_obj_add_state(btns[i], LV_STATE_DISABLED) : lv_obj_clear_state(btns[i], LV_STATE_DISABLED);
        }

        if (trigger_reset_power_ui) {
            last_dist_ref_km = 0; 
            current_power_index = 0; 
            dist_resolution_m = 10.0f; 
            max_power_ref = 300.0f;
            power_accumulator = 0; 
            power_sample_count = 0;
            for (int j = 0; j < 100; j++) { 
                if (power_dots[j]) { lv_obj_add_flag(power_dots[j], LV_OBJ_FLAG_HIDDEN); }
                power_points[j].x = (lv_coord_t)(j * 9 + 4); 
                power_points[j].y = 0; 
            }
            if (power_line) { lv_obj_add_flag(power_line, LV_OBJ_FLAG_HIDDEN); }
            if (label_1000m) { lv_label_set_text(label_1000m, "1 km"); }
            if (label_mid_dist) { lv_label_set_text(label_mid_dist, ""); }
            if (label_q1_dist) { lv_label_set_text(label_q1_dist, ""); }
            if (label_q3_dist) { lv_label_set_text(label_q3_dist, ""); }
            if (v_line_mid) { lv_obj_set_style_bg_color(v_line_mid, lv_color_hex(0x444444), 0); }
            if (v_line_q1) { lv_obj_set_style_bg_color(v_line_q1, lv_color_hex(0x444444), 0); }
            if (v_line_q3) { lv_obj_set_style_bg_color(v_line_q3, lv_color_hex(0x444444), 0); }
        }

        if (update_power_chart) {
            if (avg_power_to_render > max_power_ref) {
                max_power_ref = (float)((int)(avg_power_to_render / 50.0f) + 1) * 50.0f;
                for (int j = 0; j < current_power_index; j++) {
                    int py = 190 - (int)(power_history[j] / max_power_ref * 170.0f) - 10;
                    if (py < 10) { py = 10; }
                    if (py > 180) { py = 180; }
                    lv_obj_set_y(power_dots[j], py); power_points[j].y = (lv_coord_t)(py + 2);
                }
            }
            if (current_power_index < POWER_HISTORY_SIZE) {
                power_history[current_power_index] = avg_power_to_render;
                int py = 190 - (int)(avg_power_to_render / max_power_ref * 170.0f) - 10;
                if (py < 10) { py = 10; }
                if (py > 180) { py = 180; }
                lv_obj_set_y(power_dots[current_power_index], py);
                lv_obj_clear_flag(power_dots[current_power_index], LV_OBJ_FLAG_HIDDEN);
                power_points[current_power_index].x = (lv_coord_t)(current_power_index * 9 + 4);
                power_points[current_power_index].y = (lv_coord_t)(py + 2);
                current_power_index++;
                if (current_power_index > 1) { lv_line_set_points(power_line, power_points, (uint16_t)current_power_index); lv_obj_clear_flag(power_line, LV_OBJ_FLAG_HIDDEN); }
            } else {
                dist_resolution_m *= 2.0f; 
                lv_label_set_text_fmt(label_1000m, "%d km", (int)(dist_resolution_m * 100 / 1000));
                
                int total_val_m = (int)(dist_resolution_m * 100);
                int mid_val_m = total_val_m / 2;
                int q1_val_m = total_val_m / 4;
                int q3_val_m = (total_val_m * 3) / 4;

                // DinÃ¡mica de etiquetas y colores de lÃ­nea
                if (mid_val_m < 1000) {
                    lv_label_set_text(label_mid_dist, "");
                    if (v_line_mid) lv_obj_set_style_bg_color(v_line_mid, lv_color_hex(0x444444), 0);
                } else {
                    lv_label_set_text_fmt(label_mid_dist, "%d km", mid_val_m / 1000);
                    if (v_line_mid) lv_obj_set_style_bg_color(v_line_mid, lv_color_hex(0x888888), 0);
                }

                if (total_val_m < 4000) {
                    lv_label_set_text(label_q1_dist, "");
                    lv_label_set_text(label_q3_dist, "");
                    if (v_line_q1) lv_obj_set_style_bg_color(v_line_q1, lv_color_hex(0x444444), 0);
                    if (v_line_q3) lv_obj_set_style_bg_color(v_line_q3, lv_color_hex(0x444444), 0);
                } else {
                    lv_label_set_text_fmt(label_q1_dist, "%d km", q1_val_m / 1000);
                    lv_label_set_text_fmt(label_q3_dist, "%d km", q3_val_m / 1000);
                    if (v_line_q1) lv_obj_set_style_bg_color(v_line_q1, lv_color_hex(0x888888), 0);
                    if (v_line_q3) lv_obj_set_style_bg_color(v_line_q3, lv_color_hex(0x888888), 0);
                }

                for (int j = 0; j < 50; j++) {
                    power_history[j] = (power_history[2 * j] + power_history[2 * j + 1]) / 2.0f;
                    int py = 190 - (int)(power_history[j] / max_power_ref * 170.0f) - 10;
                    lv_obj_set_y(power_dots[j], py); power_points[j].x = (lv_coord_t)(j * 9 + 4); power_points[j].y = (lv_coord_t)(py + 2);
                }
                for (int j = 50; j < POWER_HISTORY_SIZE; j++) lv_obj_add_flag(power_dots[j], LV_OBJ_FLAG_HIDDEN);
                current_power_index = 50; lv_line_set_points(power_line, power_points, (uint16_t)current_power_index);
            }
        }

        if (update_cal_ui) {
            lv_label_set_text(label_cal_status, cal_status_text);
            if (s_cal_seq != CAL_RAMP_DOWN && s_cal_seq != CAL_FINISHED) {
                lv_label_set_text_fmt(label_cal_speed, "%d.0", s_cal_current_kmh);
                lv_bar_set_value(bar_cal_progress, s_cal_current_kmh, LV_ANIM_OFF);
            }
            if (cal_load_settings) lv_scr_load(scr_app_settings);
        }

        if (snapshot.elapsed_seconds != prev_elapsed_seconds) {
            uint32_t h = snapshot.elapsed_seconds / 3600; uint32_t m = (snapshot.elapsed_seconds % 3600) / 60; uint32_t s = snapshot.elapsed_seconds % 60;
            lv_label_set_text_fmt(label_time, "%u:%02u:%02u", (unsigned int)h, (unsigned int)m, (unsigned int)s);
            lv_label_set_text_fmt(label_time_set, "%u:%02u:%02u", (unsigned int)h, (unsigned int)m, (unsigned int)s);
            prev_elapsed_seconds = snapshot.elapsed_seconds;
        }

        int dist_meters = (int)(snapshot.total_distance_km * 1000);
        if (dist_meters != prev_dist_value) {
            if (snapshot.total_distance_km < 1.0) { 
                lv_label_set_text_fmt(label_dist, "%d", dist_meters); 
                lv_label_set_text_fmt(label_dist_set, "%d", dist_meters); 
            } else { 
                int di = (int)snapshot.total_distance_km; 
                int df = (int)fabs((snapshot.total_distance_km - di) * 1000); 
                lv_label_set_text_fmt(label_dist, "%d.%03d", di, df); 
                lv_label_set_text_fmt(label_dist_set, "%d.%03d", di, df); 
            }
            prev_dist_value = dist_meters;
        }

        int s10 = (int)roundf(snapshot.speed_kmh * 10.0f);
        if (s10/10 != prev_speed_int || s10%10 != prev_speed_frac) {
            lv_label_set_text_fmt(label_speed_kmh, "%d.%d", s10/10, s10%10);
            prev_speed_int = s10/10; prev_speed_frac = s10%10;
        }

        int c_int = (int)roundf(snapshot.target_climb_percent);
        if (c_int != prev_climb_int) { lv_label_set_text_fmt(label_climb_percent, "%d", c_int); prev_climb_int = c_int; }

        int pm = -1, ps = -1;
        if (snapshot.speed_kmh > 6.01f) { float pace = 60.0f / snapshot.speed_kmh; pm = (int)pace; ps = (int)((pace-pm)*60+0.5f); if(ps>=60){ps=0;pm++;} }
        if (pm != prev_pace_int || ps != prev_pace_frac) {
            if (pm > 0 && pm < 10) lv_label_set_text_fmt(label_speed_pace, "%d:%02d", pm, ps); else lv_label_set_text(label_speed_pace, "-:--");
            prev_pace_int = pm; prev_pace_frac = ps;
        }

        int cur_p = (snapshot.ble_connected && snapshot.real_pulse > 0) ? (int)snapshot.real_pulse : -1;
        if (cur_p != prev_pulse) { if (cur_p > 0) lv_label_set_text_fmt(label_pulse, "%d", cur_p); else lv_label_set_text(label_pulse, "--"); prev_pulse = cur_p; }

        int cur_k = (int)(snapshot.sim_kcal + 0.5f);
        if (cur_k != prev_kcal) { if (snapshot.weight_entered) lv_label_set_text_fmt(label_kcal, "%d", cur_k); else lv_label_set_text(label_kcal, "--"); prev_kcal = cur_k; }

        if (h_f_snap != head_value) { char b[2]; sprintf(b, "%d", h_f_snap); lv_label_set_text(label_head_value, b); } // Actually using them now
        if (c_f_snap != chest_value) { char b[2]; sprintf(b, "%d", c_f_snap); lv_label_set_text(label_chest_value, b); }

        if (need_restore_weight_buttons) {
            lv_label_set_text(label_stop_btn, "STOP"); lv_label_set_text(label_cooldown_btn, "COOL\nDOWN");
            lv_obj_remove_event_cb(btn_stop, back_to_training_select_event_cb); lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_remove_event_cb(btn_cooldown, weight_event_cb); lv_obj_add_event_cb(btn_cooldown, cool_down_event_cb, LV_EVENT_CLICKED, NULL);
            lv_label_set_text(unit_kcal_main, "Kcal");
            showing_weight_in_kcal_field = false;
            buttons_are_stop_mode = true; 
            need_restore_weight_buttons = false;
        }

#ifndef SIMULATOR
        if (!acoustic_service_is_calibrating() && (int)snapshot.steps != prev_steps) {
#else
        if ((int)snapshot.steps != prev_steps) {
#endif
            // if (label_stride) lv_label_set_text_fmt(label_stride, "%"PRIu32, snapshot.steps);
            // if (label_stride_set) lv_label_set_text_fmt(label_stride_set, "%"PRIu32, snapshot.steps);
            prev_steps = (int)snapshot.steps;
        }

        bsp_display_unlock();
        vTaskDelay(pdMS_TO_TICKS(UI_UPDATE_INTERVAL_MS));
    }
}

//==================================================================================
// 5. ACTUALIZACIÃ“N VISUAL DE BOTONES
//==================================================================================

// Actualiza la opacidad de los botones +/- y SET segÃºn el estado RESUME
static void update_button_states_visual(void) {
    // Leer estado actual con protecciÃ³n de mutex
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool is_stopped = g_treadmill_state.is_stopped;
    bool is_cooling_down = g_treadmill_state.is_cooling_down;
    xSemaphoreGive(g_state_mutex);

    // Si hay algÃºn RESUME activo (STOP o COOL DOWN), deshabilitar visualmente +/-, SET
    if (is_stopped || is_cooling_down) {
        // Reducir opacidad al 30% para botones deshabilitados
        lv_obj_set_style_opa(btn_speed_inc, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_speed_set, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_speed_dec, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_climb_inc, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_climb_set, LV_OPA_30, 0);
        lv_obj_set_style_opa(btn_climb_dec, LV_OPA_30, 0);

        // NO reducir opacidad de los botones END - deben verse completamente opacos en rojo
        // Solo deshabilitar el botÃ³n contrario (el que no es END)
        // El botÃ³n END mantiene su opacidad completa para que se vea el rojo brillante
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
        audio_play_beep(); // Should be outside mutex
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
        audio_play_beep();
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
    // 1. Detener la cinta y actualizar estado
    cm_master_set_speed(0.0f);
    cm_master_set_training_mode(false);

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.target_speed = 0.0f;
    g_treadmill_state.is_stopped = true;
    g_treadmill_state.is_cooling_down = false;
    g_treadmill_state.is_resuming = false;
    g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
    // NO resetear elapsed_seconds ni total_distance_km - solo el botÃ³n "Puesta a cero" debe hacer eso
    bool should_show_upload = g_treadmill_state.has_run_minimum_time &&
                              !g_treadmill_state.has_uploaded &&
                              (g_treadmill_state.selected_training == 2 || g_treadmill_state.selected_training == 3);
    xSemaphoreGive(g_state_mutex);

    // 2. Limpiar timer de WiFi si existe (desde contexto LVGL, no lock necesario si llamamos a lv_timer_del directamente)
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }

    // Resetear timestamp WiFi
    wifi_connected_timestamp = 0;

    if (should_show_upload) {
        lv_obj_clear_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Mostrando botÃ³n UPLOAD en pantalla inicial (desde END)");
    } else {
        lv_obj_add_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
    }

    // Volver a la pantalla de selecciÃ³n de entrenamiento
    lv_scr_load(scr_training_select);

    // Recrear timer WiFi
    wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 100, NULL);
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

    // --- ACTUALIZACIÃ“N DE UI A MODO PAUSA ---
    lv_label_set_text(label_stop_btn, "RESUME");
    lv_label_set_text(label_cooldown_btn, "END");
    
    lv_obj_remove_event_cb(btn_stop, stop_resume_event_cb);
    lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
    lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
    lv_obj_remove_event_cb(btn_cooldown, stop_from_cooldown_event_cb);
    lv_obj_add_event_cb(btn_cooldown, end_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Estilo rojo para el botÃ³n END (tal cual hace el STOP normal)
    lv_obj_set_style_bg_color(btn_cooldown, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_cooldown, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_cooldown_btn, lv_color_hex(0xFFFFFF), 0);
    
    set_info_text_persistent("Ejercicio en pausa. Pulsa RESUME para continuar o END para finalizar.");

    lv_obj_invalidate(btn_stop);
    lv_obj_invalidate(btn_cooldown);
    update_button_states_visual();
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

    // Parar telemetrÃ­a y obtener el reporte HD
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

static void calibration_cancel_event_cb(lv_event_t *e) {
    audio_play_beep();
    cm_master_set_speed(0.0f);
    s_cal_seq = CAL_IDLE;
    lv_scr_load(scr_app_settings);
}

static void calibration_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        audio_play_beep();
        s_cal_seq = CAL_WAITING_USER_OFF;
        lv_scr_load(scr_calibration);
        cm_master_set_training_mode(true);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        audio_play_beep();
        // Disparar captura de audio para diagnÃ³stico por puerto serie
        acoustic_service_dump_samples();
        set_info_text("Capturando 2s de audio... Revisa el monitor serie.");
    }
}

//==================================================================================
// 6. FUNCIONES DE CREACIÃ“N DE INTERFAZ
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
    lv_style_set_text_color(&style_btn_text_disabled, lv_color_hex(0x000000)); // Negro cuando estÃ¡ desactivo
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
    lv_label_set_text(panels.stride_label, ""); // Empty text
    lv_obj_align_to(panels.stride_label, panels.time_label, LV_ALIGN_OUT_LEFT_MID, -348, 0); // Moved 5mm left (-320-28)
    lv_obj_set_width(panels.stride_label, 150);
    lv_obj_set_style_text_align(panels.stride_label, LV_TEXT_ALIGN_RIGHT, 0);

    // Static circle (9mm ~ 50px)
    stride_circle = lv_obj_create(parent); // Assign to global
    lv_obj_set_size(stride_circle, 50, 50);
    lv_obj_set_style_radius(stride_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(stride_circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stride_circle, 1, 0);
    lv_obj_set_style_border_color(stride_circle, lv_color_white(), 0);
    // Align centered where the value was. Using panels.stride_label as anchor.
    // panels.stride_label is 150px wide, aligned RIGHT. 
    // We want the circle to be roughly where the number "123" was.
    // Since text align is right, "123" is at the right side of the 150px box.
    // Let's align the circle to the right side of panels.stride_label.
    lv_obj_align_to(stride_circle, panels.stride_label, LV_ALIGN_RIGHT_MID, -10, 0); // Slight offset from right edge

    lv_obj_t* unit_stride = lv_label_create(parent);
    lv_obj_add_style(unit_stride, &style_unit, 0);
    lv_label_set_text(unit_stride, "Pasos");
    lv_obj_align_to(unit_stride, panels.stride_label, LV_ALIGN_OUT_BOTTOM_LEFT, -11, 5); // Moved 0.5mm right (-14+3)
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

    // Guardar referencias segÃºn la pantalla
    if (parent == scr_main) {
        unit_kcal_main = unit_kcal;
        stride_circle_main = stride_circle; // Capture the main screen circle
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

    // --- COLUMNA DE INCLINACIÃ“N (CLIMB) --- - Subido 4mm (22px)
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
        bsp_display_lock(portMAX_DELAY);
        lv_obj_add_flag(spinner_ble_scan, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
    }

    bsp_display_lock(portMAX_DELAY);

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
    
    bsp_display_lock(portMAX_DELAY);
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


// CALLBACKS PARA PANTALLA DE SELECCIÃ“N DE ENTRENAMIENTO
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
    ESP_LOGI(TAG, "Saliendo de pantalla principal - Training mode, TelemetrÃ­a y Plan desactivados");

    // Limpiar timer de WiFi si existe
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }
    // Resetear timestamp WiFi
    wifi_connected_timestamp = 0;

    // Verificar si se debe mostrar el botÃ³n UPLOAD al volver a la pantalla inicial
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool should_show_upload = g_treadmill_state.has_run_minimum_time &&
                              !g_treadmill_state.has_uploaded &&
                              (g_treadmill_state.selected_training == 2 || g_treadmill_state.selected_training == 3);
    xSemaphoreGive(g_state_mutex);

    if (should_show_upload) {
        lv_obj_clear_flag(btn_upload_training, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "Mostrando botÃ³n UPLOAD en pantalla inicial");
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
    int val = (int)lv_slider_get_value(slider);
    lv_label_set_text_fmt(label_sensitivity_pct, "%d%%", val);
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.pedometer_sensitivity = (uint8_t)val;
    xSemaphoreGive(g_state_mutex);
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
    
    // Initialize defaults for the configuration if it's the first time
    if (g_series_config.num_vueltas == 0) {
        g_series_config.num_vueltas = 1;
        g_series_config.work.speed = 10.0f;
        g_series_config.work.climb = 0;
        g_series_config.work.duration_secs = 60;
        g_series_config.work.steps_enabled = false;
        g_series_config.work.spm = 180;
        g_series_config.work.sound_enabled = false;
        
        g_series_config.rest.speed = 5.0f;
        g_series_config.rest.climb = 0;
        g_series_config.rest.duration_secs = 30;
        g_series_config.rest.steps_enabled = false;
        g_series_config.rest.spm = 120;
        g_series_config.rest.sound_enabled = false;
    }

    if (!scr_series_config) {
        create_series_config_screen();
    } else {
        // Update the label if screen already exists
        lv_label_set_text_fmt(label_series_count, "%d", g_series_config.num_vueltas);
    }
    lv_scr_load(scr_series_config);
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


static void reset_training_event_cb(lv_event_t *e) {
    audio_play_beep();
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.elapsed_seconds = 0;
    g_treadmill_state.total_distance_km = 0.0;
    g_treadmill_state.steps = 0;
    g_treadmill_state.cadence = 0.0;
    g_treadmill_state.sim_kcal = 0.0;
    g_treadmill_state.user_weight_kg = DEFAULT_USER_WEIGHT_KG;
    g_treadmill_state.weight_entered = false;
    g_treadmill_state.has_run_minimum_time = false;
    g_treadmill_state.has_uploaded = false;
    g_treadmill_state.has_shown_welcome_message = false;
    
    // Resetear estado de pausa/cool down
    g_treadmill_state.is_stopped = false;
    g_treadmill_state.is_cooling_down = false;
    g_treadmill_state.is_resuming = false;
    g_treadmill_state.cooldown_level = 0;
    g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
    g_treadmill_state.target_speed = 0.0f;
    g_treadmill_state.speed_before_stop = 0.0f;
    
    // Si hay un plan en ejecuciÃ³n, lo reseteamos tambiÃ©n
    g_treadmill_state.plan_running = false;
    g_treadmill_state.current_block_idx = 0;
    g_treadmill_state.block_elapsed_seconds = 0;
    g_treadmill_state.block_distance_km = 0.0;
    g_treadmill_state.block_kcal = 0.0;
    xSemaphoreGive(g_state_mutex);

    // Actualizar todas las etiquetas de la UI
    if (label_time) lv_label_set_text(label_time, "00:00:00");
    if (label_time_set) lv_label_set_text(label_time_set, "00:00:00");
    
    if (label_dist) lv_label_set_text(label_dist, "0");
    if (label_dist_set) lv_label_set_text(label_dist_set, "0");
    
    if (label_kcal) lv_label_set_text(label_kcal, "0");
    if (label_kcal_set) lv_label_set_text(label_kcal_set, "0");
    
    if (label_stride) lv_label_set_text(label_stride, "0");
    if (label_stride_set) lv_label_set_text(label_stride_set, "0");
    
    if (label_pulse) lv_label_set_text(label_pulse, "--");
    if (label_pulse_set) lv_label_set_text(label_pulse_set, "--");

    if (label_speed_kmh) lv_label_set_text(label_speed_kmh, "0.0");
    if (label_speed_kmh_set) lv_label_set_text(label_speed_kmh_set, "0.0");

    if (label_speed_pace) lv_label_set_text(label_speed_pace, "-:--");
    if (label_speed_pace_set) lv_label_set_text(label_speed_pace_set, "-:--");

    // Resetear botones a estado inicial (ATRAS y PESO)
    if (label_stop_btn) lv_label_set_text(label_stop_btn, "ATRAS");
    if (label_cooldown_btn) lv_label_set_text(label_cooldown_btn, "PESO");
    
    // Restaurar callbacks iniciales
    if (btn_stop) {
        lv_obj_remove_event_cb(btn_stop, stop_resume_event_cb);
        lv_obj_remove_event_cb(btn_stop, end_event_cb);
        lv_obj_add_event_cb(btn_stop, back_to_training_select_event_cb, LV_EVENT_CLICKED, NULL);
    }
    
    if (btn_cooldown) {
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, stop_from_cooldown_event_cb);
        lv_obj_add_event_cb(btn_cooldown, weight_event_cb, LV_EVENT_CLICKED, NULL);
    }
    
    // Restaurar estilos de botones (quitar rojo si lo hubiera)
    if (btn_stop) {
        lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (btn_cooldown) {
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // Resetear flag de modo botones
    buttons_are_stop_mode = false;
    showing_weight_in_kcal_field = false;
    
    // Restaurar unidad de kcal
    if (unit_kcal_main) lv_label_set_text(unit_kcal_main, "Kcal");

    if (ta_info) lv_label_set_text(ta_info, "Sesion reiniciada. Listo para nuevo corredor.");
    
    ESP_LOGI(TAG, "Cinta puesta a cero manualmente - UI y estado completamente reseteados");
}

//==================================================================================
// CREACIÃ“N DE PANTALLA DE SELECCIÃ“N
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

    // BotÃ³n 1: Entrenamiento libre
    btn = lv_btn_create(btn_container);
    lv_obj_set_size(btn, left_btn_w, btn_h);
    lv_obj_add_style(btn, &style_btn_premium, 0); 
    lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED); 
    lv_obj_add_event_cb(btn, training_free_event_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(l, 0, 0); // Asegurar que el label no tenga fondo (evita recuadro gris)
    lv_label_set_text(l, "Entreno Libre");
    lv_obj_center(l);

    btn_training_itsaso = lv_btn_create(btn_container);
    lv_obj_set_size(btn_training_itsaso, left_btn_w, btn_h);
    lv_obj_add_style(btn_training_itsaso, &style_btn_premium, 0); 
    lv_obj_add_style(btn_training_itsaso, &style_btn_premium_disabled, LV_STATE_DISABLED); 
    lv_obj_add_event_cb(btn_training_itsaso, training_itsaso_event_cb, LV_EVENT_CLICKED, NULL);
    label_training_itsaso = lv_label_create(btn_training_itsaso);
    lv_obj_add_style(label_training_itsaso, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(label_training_itsaso, 0, 0); 
    lv_label_set_text(label_training_itsaso, "Definir Entreno");
    lv_obj_center(label_training_itsaso);
    lv_obj_add_flag(btn_training_itsaso, LV_OBJ_FLAG_CLICKABLE); // Ahora siempre disponible
    lv_obj_set_style_text_color(label_training_itsaso, lv_color_hex(0xFFFFFF), 0); // Blanco inicial

    // BotÃ³n 3: Entrenamiento Ina
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
    lv_obj_clear_flag(btn_training_ina, LV_OBJ_FLAG_CLICKABLE); // Deshabilitado lÃ³gicamente
    lv_obj_set_style_text_color(label_training_ina, lv_color_hex(0x000000), 0); // Negro inicial

    // BotÃ³n 4: Puesta a cero (idÃ©ntico en tamaÃ±o y diseÃ±o)
    btn = lv_btn_create(btn_container);
    lv_obj_set_size(btn, left_btn_w, btn_h);
    lv_obj_add_style(btn, &style_btn_premium, 0); 
    lv_obj_add_style(btn, &style_btn_premium_disabled, LV_STATE_DISABLED); 
    lv_obj_add_event_cb(btn, reset_training_event_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(btn);
    lv_obj_add_style(l, &style_btn_text, 0);
    lv_obj_set_style_bg_opa(l, 0, 0); 
    lv_label_set_text(l, "Nuevo Entreno");
    lv_obj_center(l);


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
    // Aplicar color rojo sÃ³lido
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

    label_status_wax = lv_label_create(scr_training_select);
    lv_obj_add_style(label_status_wax, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_status_wax, &lv_font_montserrat_20, 0); // Increasged size
    lv_obj_set_style_text_color(label_status_wax, lv_color_hex(0xFF0000), 0); // Red
    lv_label_set_text(label_status_wax, "Intervalo WAX excedido");
    lv_obj_align(label_status_wax, LV_ALIGN_BOTTOM_MID, 0, -85); // Slightly adjusted for larger font

    // Crear timer para verificar estado de WiFi cada 100ms
    wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 100, NULL);

    // --- ICONO CANAL CENTRAL ---
    lv_obj_t *img_icon = lv_img_create(scr_training_select);
    lv_img_set_src(img_icon, &icon_main);
    lv_obj_align(img_icon, LV_ALIGN_CENTER, 0, -150);
}

//==================================================================================
// CREACIÃ“N DE PANTALLA DE CARGA
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

    // DivisiÃ³n en 100 franjas (99 lÃ­neas) - Todas exactamente iguales (8px de espacio + 1px de lÃ­nea)
    for (int i = 1; i < 100; i++) {
        lv_obj_t *v_line = lv_obj_create(dark_box);
        lv_obj_set_size(v_line, 1, 190);
        lv_obj_set_pos(v_line, i * 9 - 1, 0); // Stride de 9px garantiza 8px de espacio entre lÃ­neas
        
        // Initial setup for scale 1km: all lines dark gray
        lv_obj_set_style_bg_color(v_line, lv_color_hex(0x444444), 0);
        if (i == 50) v_line_mid = v_line;
        else if (i == 25) v_line_q1 = v_line;
        else if (i == 75) v_line_q3 = v_line;
        
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

    // Crear la lÃ­nea continua que une los puntos
    power_line = lv_line_create(dark_box);
    lv_obj_set_style_line_width(power_line, 1, 0); // LÃ­nea fina
    lv_obj_set_style_line_color(power_line, lv_color_hex(0x00FF00), 0); // Mismo verde
    lv_obj_set_style_line_opa(power_line, LV_OPA_COVER, 0);
    lv_obj_add_flag(power_line, LV_OBJ_FLAG_HIDDEN); // Oculta al inicio

    lv_obj_t *label_0m = lv_label_create(scr_main);
    lv_obj_add_style(label_0m, &style_unit, 0);
    lv_label_set_text(label_0m, "0 m");
    lv_obj_align_to(label_0m, dark_box, LV_ALIGN_OUT_BOTTOM_LEFT, -14, 5); // Moved another 0.5mm left

    label_1000m = lv_label_create(scr_main);
    lv_obj_add_style(label_1000m, &style_unit, 0);
    lv_label_set_text(label_1000m, "1 km");
    lv_obj_align_to(label_1000m, dark_box, LV_ALIGN_OUT_BOTTOM_RIGHT, 25, 5); // Moved another 0.5mm left (28-3)

    label_mid_dist = lv_label_create(scr_main);
    lv_obj_add_style(label_mid_dist, &style_unit, 0);
    lv_label_set_text(label_mid_dist, "");
    lv_obj_align_to(label_mid_dist, dark_box, LV_ALIGN_OUT_BOTTOM_MID, -12, 5); // Moved another 0.5mm left (-9-3)

    label_q1_dist = lv_label_create(scr_main);
    lv_obj_add_style(label_q1_dist, &style_unit, 0);
    lv_label_set_text(label_q1_dist, "");
    lv_obj_align_to(label_q1_dist, dark_box, LV_ALIGN_OUT_BOTTOM_LEFT, 224 - 14, 5); // Moved another 0.5mm left

    label_q3_dist = lv_label_create(scr_main);
    lv_obj_add_style(label_q3_dist, &style_unit, 0);
    lv_label_set_text(label_q3_dist, "");
    lv_obj_align_to(label_q3_dist, dark_box, LV_ALIGN_OUT_BOTTOM_LEFT, 674 - 14, 5); // Moved another 0.5mm left

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

    // BotÃ³n CHEST
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
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botÃ³n
    label_chest_value = lv_label_create(btn);
    lv_obj_add_style(label_chest_value, &style_btn_text, 0);
    lv_label_set_text(label_chest_value, "0");
    lv_obj_add_flag(label_chest_value, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botÃ³n

    btn_stop = lv_btn_create(left_col); lv_obj_set_size(btn_stop, btn_w, btn_h); lv_obj_add_style(btn_stop, &style_btn_premium, 0); lv_obj_add_style(btn_stop, &style_btn_premium_disabled, LV_STATE_DISABLED);
    label_stop_btn = lv_label_create(btn_stop);
    lv_obj_add_style(label_stop_btn, &style_btn_text, 0);
    lv_label_set_text(label_stop_btn, "STOP");
    lv_obj_center(label_stop_btn);
    // Callback se aÃ±adirÃ¡ despuÃ©s segÃºn weight_entered

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

    // BotÃ³n HEAD
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
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botÃ³n
    label_head_value = lv_label_create(btn);
    lv_obj_add_style(label_head_value, &style_btn_text, 0);
    lv_label_set_text(label_head_value, "0");
    lv_obj_add_flag(label_head_value, LV_OBJ_FLAG_EVENT_BUBBLE);  // Permitir que eventos pasen al botÃ³n

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

    // --- CreaciÃ³n del teclado numÃ©rico ---
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
    lv_obj_set_style_bg_color(scr_wax, lv_color_black(), 0);

    // TÃ­tulo
    lv_obj_t *title = lv_label_create(scr_wax);
    lv_obj_add_style(title, &style_title, 0);
    lv_label_set_text(title, "MANTENIMIENTO WAX");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *cont = lv_obj_create(scr_wax);
    lv_obj_set_size(cont, 700, 320); // Wider to accommodate long labels
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_opa(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 15, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF); // Remove scrollbars

    const char *metric_names[] = {
        "Dias desde WAX:", 
        "Tiempo de uso desde WAX:", 
        "Distancia recorrida desde WAX:"
    };
    lv_obj_t **metric_labels[] = {&label_wax_days, &label_wax_usage_time, &label_wax_dist};

    for(int i = 0; i < 3; i++) {
        lv_obj_t *metric_group = lv_obj_create(cont);
        lv_obj_set_size(metric_group, LV_PCT(100), 90);
        lv_obj_set_style_bg_opa(metric_group, 0, 0);
        lv_obj_set_style_border_opa(metric_group, 0, 0);
        lv_obj_set_flex_flow(metric_group, LV_FLEX_FLOW_COLUMN); // Stack vertically
        lv_obj_set_flex_align(metric_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(metric_group, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t *l_item = lv_label_create(metric_group);
        lv_obj_add_style(l_item, &style_btn_text, 0);
        lv_obj_set_style_text_font(l_item, &lv_font_montserrat_20, 0); // Slightly smaller for the long labels
        lv_label_set_text(l_item, metric_names[i]);

        lv_obj_t *l_val = lv_label_create(metric_group);
        *metric_labels[i] = l_val;
        lv_obj_add_style(l_val, &style_value_main, 0);
        lv_obj_set_style_text_font(l_val, &lv_font_montserrat_28, 0);
        if (i == 0) lv_label_set_text(l_val, "0"); // Dias
        else if (i == 1) lv_label_set_text(l_val, "0 h  0 min"); // Tiempo
        else lv_label_set_text(l_val, "0 km"); // Distancia
    }

    // Fila de controles de cinta
    lv_obj_t *control_row = lv_obj_create(scr_wax);
    lv_obj_set_size(control_row, LV_PCT(100), 100);
    lv_obj_align(control_row, LV_ALIGN_BOTTOM_MID, 0, -108); // Raised 5mm (28px approx) from -80
    lv_obj_set_style_bg_opa(control_row, 0, 0);
    lv_obj_set_style_border_opa(control_row, 0, 0);
    lv_obj_set_flex_flow(control_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(control_row, 15, 0);
    lv_obj_set_scrollbar_mode(control_row, LV_SCROLLBAR_MODE_OFF);

    // 1. CINTA 4 KM/H
    lv_obj_t *btn_4kmh = lv_btn_create(control_row);
    lv_obj_set_size(btn_4kmh, 240, 70);
    lv_obj_add_style(btn_4kmh, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_4kmh, wax_4kmh_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_4k = lv_label_create(btn_4kmh);
    lv_obj_add_style(l_4k, &style_btn_text, 0);
    lv_label_set_text(l_4k, "CINTA 4 KM/H");
    lv_obj_center(l_4k);

    // 2. APLICAR WAX
    btn_apply_wax = lv_btn_create(control_row);
    lv_obj_set_size(btn_apply_wax, 280, 70);
    lv_obj_add_style(btn_apply_wax, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_apply_wax, apply_wax_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_app = lv_label_create(btn_apply_wax);
    lv_obj_add_style(l_app, &style_btn_text, 0);
    lv_label_set_text(l_app, "APLICAR WAX");
    lv_obj_center(l_app);

    // 3. PARAR CINTA
    lv_obj_t *btn_stop_wax = lv_btn_create(control_row);
    lv_obj_set_size(btn_stop_wax, 240, 70);
    lv_obj_add_style(btn_stop_wax, &style_btn_premium, 0); // Mismo estilo premium que el resto
    lv_obj_add_event_cb(btn_stop_wax, wax_stop_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_stop = lv_label_create(btn_stop_wax);
    lv_obj_add_style(l_stop, &style_btn_text, 0);
    lv_label_set_text(l_stop, "PARAR CINTA");
    lv_obj_center(l_stop);

    // BotÃ³n Volver (mÃ¡s abajo, mismo tamaÃ±o que los de control)
    btn_wax_back = lv_btn_create(scr_wax);
    lv_obj_set_size(btn_wax_back, 240, 70); // Identical to others
    lv_obj_add_style(btn_wax_back, &style_btn_premium, 0);
    lv_obj_align(btn_wax_back, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn_wax_back, wax_back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l_back = lv_label_create(btn_wax_back);
    lv_obj_add_style(l_back, &style_btn_text, 0);
    lv_obj_set_style_text_font(l_back, &lv_font_montserrat_26, 0); // Montserrat 26 to match premium style
    lv_label_set_text(l_back, "VOLVER");
    lv_obj_center(l_back);
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
    lv_label_set_text(label, "Entrenamiento enviado con Ã©xito,\npuedes apagar la cinta con seguridad.");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}


//==================================================================================
// 7. FUNCIONES DE GESTIÃ“N DE PANTALLA Y CURSOR
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
    
    // Confirmar automÃ¡ticamente
    ui_confirm_set_value();
}

static void _update_set_display_text_internal(void) {
    char display_buf[10];
    char d1 = (g_treadmill_state.set_digit_index > 0) ? g_treadmill_state.set_buffer[0] : '-';
    char d2 = (g_treadmill_state.set_digit_index > 1) ? g_treadmill_state.set_buffer[1] : '-';
    char d3 = (g_treadmill_state.set_digit_index > 2) ? g_treadmill_state.set_buffer[2] : '-';

    char cursor = g_treadmill_state.blink_state ? '_' : ' ';
    if (g_treadmill_state.set_digit_index == 0) d1 = cursor;
    else if (g_treadmill_state.set_digit_index == 1) d2 = cursor;
    else if (g_treadmill_state.set_digit_index == 2) d3 = cursor;

    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT) {
        sprintf(display_buf, "%c%c", d1, d2);
        lv_label_set_text(label_kcal_set, display_buf);
    } 
    else if (g_treadmill_state.set_mode == SET_MODE_CLIMB || g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_CLIMB || g_treadmill_state.set_mode == SET_MODE_SERIES_REST_CLIMB) {
        sprintf(display_buf, "%c%c", d1, d2);
        lv_label_set_text(label_climb_percent_set, display_buf);
    }
    else if (g_treadmill_state.set_mode == SET_MODE_SPEED || g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_SPEED || g_treadmill_state.set_mode == SET_MODE_SERIES_REST_SPEED) {
        sprintf(display_buf, "%c%c", d1, d2);
        lv_label_set_text(label_speed_kmh_set, display_buf);
    }
    else if (g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_TIME || g_treadmill_state.set_mode == SET_MODE_SERIES_REST_TIME) {
        // Time uses 3 digits
        sprintf(display_buf, "%c%c%c", d1, d2, d3);
        // Note: we don't have a label_time_set that looks like "--:--:--" for digit-by-digit entry easily
        // but we can use label_time_set and show the seconds there.
        lv_label_set_text(label_time_set, display_buf);
    }
    else if (g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_SPM || g_treadmill_state.set_mode == SET_MODE_SERIES_REST_SPM) {
        // SPM uses 3 digits
        sprintf(display_buf, "%c%c%c", d1, d2, d3);
        lv_label_set_text(label_stride_set, display_buf);
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
        lv_label_set_text(unit_kcal_set, "kg");
    } else if (mode == SET_MODE_SERIES_WORK_SPEED) {
        lv_label_set_text(ta_info_set, "Introduce VELOCIDAD de TRABAJO (km/h).");
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_SERIES_WORK_CLIMB) {
        lv_label_set_text(ta_info_set, "Introduce INCLINACION de TRABAJO (%).");
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_SERIES_WORK_TIME) {
        lv_label_set_text(ta_info_set, "Introduce TIEMPO de TRABAJO (segundos).");
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_SERIES_WORK_SPM) {
        lv_label_set_text(ta_info_set, "Introduce CADENCIA de TRABAJO (SPM).");
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_SERIES_REST_SPEED) {
        lv_label_set_text(ta_info_set, "Introduce VELOCIDAD de DESCANSO (km/h).");
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_SERIES_REST_CLIMB) {
        lv_label_set_text(ta_info_set, "Introduce INCLINACION de DESCANSO (%).");
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_SERIES_REST_TIME) {
        lv_label_set_text(ta_info_set, "Introduce TIEMPO de DESCANSO (segundos).");
        lv_label_set_text(unit_kcal_set, "Kcal");
    } else if (mode == SET_MODE_SERIES_REST_SPM) {
        lv_label_set_text(ta_info_set, "Introduce CADENCIA de DESCANSO (SPM).");
        lv_label_set_text(unit_kcal_set, "Kcal");
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

    // Forzar actualizaciÃ³n de los displays con los valores actuales confirmados
    int speed_int = (int)g_treadmill_state.speed_kmh;
    int speed_frac = (int)fabs((g_treadmill_state.speed_kmh - speed_int) * 10);
    lv_label_set_text_fmt(label_speed_kmh_set, "%d.%d", speed_int, speed_frac);

    int climb_int = (int)roundf(g_treadmill_state.climb_percent);
    lv_label_set_text_fmt(label_climb_percent_set, "%d", climb_int);
}


// --- Overlay Numpad Callbacks ---

// Overlay Numpad Callbacks
static void overlay_numpad_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    audio_play_beep();
    
    int len = strlen(overlay_numpad_buffer);

    if (strcmp(txt, "DEL") == 0) {
        if (len > 0) overlay_numpad_buffer[len - 1] = '\0';
    } 
    else if (strcmp(txt, ".") == 0) {
        // Only for speed and if not already present
        if ((overlay_numpad_mode == SET_MODE_SERIES_WORK_SPEED || overlay_numpad_mode == SET_MODE_SERIES_REST_SPEED) && 
            !strchr(overlay_numpad_buffer, '.') && len < 4) {
            strcat(overlay_numpad_buffer, ".");
        }
    }
    else {
        // Digit entry
        if (overlay_numpad_mode == SET_MODE_SERIES_WORK_TIME || overlay_numpad_mode == SET_MODE_SERIES_REST_TIME) {
            // Shift-left logic: max 4 digits (MMSS)
            if (len < 4) {
                strcat(overlay_numpad_buffer, txt);
            }
        } else {
            // Normal entry
            int max_len = 3; // Default (SPM)
            if (overlay_numpad_mode == SET_MODE_SERIES_WORK_CLIMB || overlay_numpad_mode == SET_MODE_SERIES_REST_CLIMB) max_len = 2;
            if (overlay_numpad_mode == SET_MODE_SERIES_WORK_SPEED || overlay_numpad_mode == SET_MODE_SERIES_REST_SPEED) max_len = 4; // e.g. 19.5

            if (len < max_len) {
                strcat(overlay_numpad_buffer, txt);
            }
        }
    }
    
    // Update display label
    if (strlen(overlay_numpad_buffer) == 0) {
        lv_label_set_text(label_overlay_numpad_value, "0");
    } else if (overlay_numpad_mode == SET_MODE_SERIES_WORK_TIME || overlay_numpad_mode == SET_MODE_SERIES_REST_TIME) {
        // Format as MM:SS
        char fmt[8];
        int l = strlen(overlay_numpad_buffer);
        if (l == 1) sprintf(fmt, "00:0%s", overlay_numpad_buffer);
        else if (l == 2) sprintf(fmt, "00:%s", overlay_numpad_buffer);
        else if (l == 3) sprintf(fmt, "0%c:%s", overlay_numpad_buffer[0], &overlay_numpad_buffer[1]);
        else if (l == 4) sprintf(fmt, "%c%c:%c%c", overlay_numpad_buffer[0], overlay_numpad_buffer[1], overlay_numpad_buffer[2], overlay_numpad_buffer[3]);
        lv_label_set_text(label_overlay_numpad_value, fmt);
    } else {
        lv_label_set_text(label_overlay_numpad_value, overlay_numpad_buffer);
    }
}

static void overlay_numpad_cancel_cb(lv_event_t *e) {
    audio_play_beep();
    // Hide overlay without saving
    lv_obj_add_flag(overlay_numpad_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay_numpad_container, LV_OBJ_FLAG_HIDDEN);
}

static void overlay_numpad_confirm_cb(lv_event_t *e) {
    audio_play_beep();
    
    float val = 0;
    if (overlay_numpad_mode == SET_MODE_SERIES_WORK_TIME || overlay_numpad_mode == SET_MODE_SERIES_REST_TIME) {
        // MM:SS format -> seconds
        int mm = 0, ss = 0;
        if (strlen(overlay_numpad_buffer) >= 3) {
            char ss_str[3], mm_str[3];
            int len = strlen(overlay_numpad_buffer);
            strcpy(ss_str, &overlay_numpad_buffer[len-2]);
            int mm_len = len - 2;
            if (mm_len > 2) mm_len = 2; // Safety check
            memcpy(mm_str, overlay_numpad_buffer, mm_len);
            mm_str[mm_len] = '\0';
            mm = atoi(mm_str);
            ss = atoi(ss_str);
        } else {
            ss = atoi(overlay_numpad_buffer);
        }
        val = (float)(mm * 60 + ss);
    } else {
        val = atof(overlay_numpad_buffer);
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    switch (overlay_numpad_mode) {
        case SET_MODE_SERIES_WORK_SPEED: 
            if (val > MAX_SPEED_KMH) val = MAX_SPEED_KMH;
            if (val < 1.0f) val = 1.0f;
            g_series_config.work.speed = val; 
            break;
        case SET_MODE_SERIES_WORK_CLIMB: 
            if (val > MAX_CLIMB_PERCENT) val = MAX_CLIMB_PERCENT;
            g_series_config.work.climb = (int)val; 
            break;
        case SET_MODE_SERIES_WORK_TIME:  
            g_series_config.work.duration_secs = (uint32_t)val; 
            break;
        case SET_MODE_SERIES_WORK_SPM:   
            g_series_config.work.spm = (int)val; 
            break;
        case SET_MODE_SERIES_REST_SPEED: 
            if (val > MAX_SPEED_KMH) val = MAX_SPEED_KMH;
            if (val < 0.5f) val = 0.5f; // Rest can be slower
            g_series_config.rest.speed = val; 
            break;
        case SET_MODE_SERIES_REST_CLIMB: 
            if (val > MAX_CLIMB_PERCENT) val = MAX_CLIMB_PERCENT;
            g_series_config.rest.climb = (int)val; 
            break;
        case SET_MODE_SERIES_REST_TIME:  
            g_series_config.rest.duration_secs = (uint32_t)val; 
            break;
        case SET_MODE_SERIES_REST_SPM:   
            g_series_config.rest.spm = (int)val; 
            break;
        default: 
            break;
    }
    xSemaphoreGive(g_state_mutex);

    // Update labels on series screen
    bsp_display_lock(portMAX_DELAY);
    switch (overlay_numpad_mode) {
        case SET_MODE_SERIES_WORK_SPEED: lv_label_set_text_fmt(label_series_work_speed, "Speed: %.1f", g_series_config.work.speed); break;
        case SET_MODE_SERIES_WORK_CLIMB: lv_label_set_text_fmt(label_series_work_climb, "Incline: %d", g_series_config.work.climb); break;
        case SET_MODE_SERIES_WORK_TIME: {
            int mm = g_series_config.work.duration_secs / 60;
            int ss = g_series_config.work.duration_secs % 60;
            lv_label_set_text_fmt(label_series_work_time, "Time: %02d:%02d", mm, ss);
            break;
        }
        case SET_MODE_SERIES_WORK_SPM:   lv_label_set_text_fmt(label_series_work_spm, "SPM: %d", g_series_config.work.spm); break;
        case SET_MODE_SERIES_REST_SPEED: lv_label_set_text_fmt(label_series_rest_speed, "Speed: %.1f", g_series_config.rest.speed); break;
        case SET_MODE_SERIES_REST_CLIMB: lv_label_set_text_fmt(label_series_rest_climb, "Incline: %d", g_series_config.rest.climb); break;
        case SET_MODE_SERIES_REST_TIME: {
            int mm = g_series_config.rest.duration_secs / 60;
            int ss = g_series_config.rest.duration_secs % 60;
            lv_label_set_text_fmt(label_series_rest_time, "Time: %02d:%02d", mm, ss);
            break;
        }
        case SET_MODE_SERIES_REST_SPM:   lv_label_set_text_fmt(label_series_rest_spm, "SPM: %d", g_series_config.rest.spm); break;
        default: break;
    }
    bsp_display_unlock();
    
    // Hide overlay
    lv_obj_add_flag(overlay_numpad_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay_numpad_container, LV_OBJ_FLAG_HIDDEN);
}

static void _show_overlay_numpad(set_mode_t mode) {
    overlay_numpad_mode = mode;
    memset(overlay_numpad_buffer, 0, sizeof(overlay_numpad_buffer));
    
    // Set title and unit based on mode
    const char *title = "CONFIGURACION";
    const char *unit = "";

    switch (mode) {
        case SET_MODE_SERIES_WORK_SPEED: title = "TRABAJO: VELOCIDAD V2"; unit = "km/h"; break;
        case SET_MODE_SERIES_WORK_CLIMB: title = "TRABAJO: INCLINACION"; unit = "%"; break;
        case SET_MODE_SERIES_WORK_TIME:  title = "TRABAJO: TIEMPO"; unit = "mm:ss"; break;
        case SET_MODE_SERIES_WORK_SPM:   title = "TRABAJO: CADENCIA"; unit = "SPM"; break;
        case SET_MODE_SERIES_REST_SPEED: title = "DESCANSO: VELOCIDAD"; unit = "km/h"; break;
        case SET_MODE_SERIES_REST_CLIMB: title = "DESCANSO: INCLINACION"; unit = "%"; break;
        case SET_MODE_SERIES_REST_TIME:  title = "DESCANSO: TIEMPO"; unit = "mm:ss"; break;
        case SET_MODE_SERIES_REST_SPM:   title = "DESCANSO: CADENCIA"; unit = "SPM"; break;
        default: 
            title = "CONFIGURACION"; unit = ""; 
            break;
    }

    lv_label_set_text(label_overlay_numpad_title, title);
    lv_label_set_text(label_overlay_numpad_unit, unit);
    lv_label_set_text(label_overlay_numpad_value, "0");

    // Position overlay based on which column is being edited
    // WORK fields -> show on RIGHT (over REST column)
    // REST fields -> show on LEFT (over WORK column)
    bool is_work_field = (mode >= SET_MODE_SERIES_WORK_SPEED && mode <= SET_MODE_SERIES_WORK_SPM);
    
    if (is_work_field) {
        // Position over REST column (right side, bottom aligned)
        lv_obj_align(overlay_numpad_container, LV_ALIGN_BOTTOM_RIGHT, -90, -143);
    } else {
        // Position over WORK column (left/center, bottom aligned)
        lv_obj_align(overlay_numpad_container, LV_ALIGN_BOTTOM_LEFT, 340, -143);
    }

    // Show the overlay
    lv_obj_clear_flag(overlay_numpad_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(overlay_numpad_container, LV_OBJ_FLAG_HIDDEN);
}

static void create_overlay_numpad(lv_obj_t *parent) {
    // Create semi-transparent background overlay
    overlay_numpad_bg = lv_obj_create(parent);
    lv_obj_set_size(overlay_numpad_bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay_numpad_bg, 0, 0);
    lv_obj_set_style_bg_color(overlay_numpad_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay_numpad_bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(overlay_numpad_bg, 0, 0);
    lv_obj_add_flag(overlay_numpad_bg, LV_OBJ_FLAG_HIDDEN); // Initially hidden
    
    // Create numpad container
    overlay_numpad_container = lv_obj_create(parent);
    lv_obj_set_size(overlay_numpad_container, 400, 550);
    lv_obj_set_style_bg_color(overlay_numpad_container, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_radius(overlay_numpad_container, 15, 0);
    lv_obj_set_style_border_color(overlay_numpad_container, lv_color_hex(0x00AA00), 0);
    lv_obj_set_style_border_width(overlay_numpad_container, 3, 0);
    lv_obj_clear_flag(overlay_numpad_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay_numpad_container, LV_OBJ_FLAG_HIDDEN); // Initially hidden
    
    // Title
    label_overlay_numpad_title = lv_label_create(overlay_numpad_container);
    lv_obj_add_style(label_overlay_numpad_title, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_overlay_numpad_title, &lv_font_montserrat_20, 0);
    lv_obj_align(label_overlay_numpad_title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Display Value Container (Centered, 300px wide)
    lv_obj_t * cont_val = lv_obj_create(overlay_numpad_container);
    lv_obj_set_size(cont_val, 300, 60);
    lv_obj_align(cont_val, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(cont_val, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(cont_val, lv_color_white(), 0);
    lv_obj_set_style_border_width(cont_val, 2, 0);
    lv_obj_clear_flag(cont_val, LV_OBJ_FLAG_SCROLLABLE);
    
    label_overlay_numpad_value = lv_label_create(cont_val);
    lv_obj_add_style(label_overlay_numpad_value, &style_value_main, 0);
    lv_obj_set_style_text_font(label_overlay_numpad_value, &lv_font_montserrat_32, 0);
    lv_label_set_text(label_overlay_numpad_value, "0");
    lv_obj_align(label_overlay_numpad_value, LV_ALIGN_CENTER, -15, 0); // Offset left to make room for units


    // Units (INSIDE the container, aligned right)
    label_overlay_numpad_unit = lv_label_create(cont_val);
    lv_obj_add_style(label_overlay_numpad_unit, &style_unit, 0);
    lv_obj_set_style_text_font(label_overlay_numpad_unit, &lv_font_montserrat_14, 0); // Smaller font for units
    lv_obj_align(label_overlay_numpad_unit, LV_ALIGN_RIGHT_MID, -10, 0);


    
    // Numpad Grid (Higher position to avoid bottom overlap)
    lv_obj_t * numpad_cont = lv_obj_create(overlay_numpad_container);
    lv_obj_set_size(numpad_cont, 320, 260);
    lv_obj_align(numpad_cont, LV_ALIGN_TOP_MID, 0, 115);
    lv_obj_set_style_bg_opa(numpad_cont, 0, 0);
    lv_obj_set_style_border_opa(numpad_cont, 0, 0);
    lv_obj_clear_flag(numpad_cont, LV_OBJ_FLAG_SCROLLABLE);
    
    const int btn_w = 95;
    const int btn_h = 52;
    const int gap = 8;
    
    // Buttons 1-9
    for(int i=1; i<=9; i++) {
        int row = (i-1)/3;
        int col = (i-1)%3;
        lv_obj_t * btn = lv_btn_create(numpad_cont);
        lv_obj_set_size(btn, btn_w, btn_h);
        lv_obj_set_pos(btn, -10 + col*(btn_w+gap), row*(btn_h+gap)); // Applied -10px offset to shift left
        lv_obj_add_style(btn, &style_btn_premium, 0);
        lv_obj_add_event_cb(btn, overlay_numpad_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t * l = lv_label_create(btn);
        lv_obj_add_style(l, &style_btn_text, 0);
        lv_label_set_text_fmt(l, "%d", i);
        lv_obj_center(l);
    }
    
    // Bottom row: . 0 DEL
    lv_obj_t * btnDot = lv_btn_create(numpad_cont);
    lv_obj_set_size(btnDot, btn_w, btn_h);
    lv_obj_set_pos(btnDot, -10 + 0*(btn_w+gap), 3*(btn_h+gap));
    lv_obj_add_style(btnDot, &style_btn_premium, 0);
    lv_obj_add_event_cb(btnDot, overlay_numpad_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lDot = lv_label_create(btnDot);
    lv_obj_add_style(lDot, &style_btn_text, 0);
    lv_label_set_text(lDot, ".");
    lv_obj_center(lDot);

    lv_obj_t * btn0 = lv_btn_create(numpad_cont);
    lv_obj_set_size(btn0, btn_w, btn_h);
    lv_obj_set_pos(btn0, -10 + 1*(btn_w+gap), 3*(btn_h+gap));
    lv_obj_add_style(btn0, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn0, overlay_numpad_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l0 = lv_label_create(btn0);
    lv_obj_add_style(l0, &style_btn_text, 0);
    lv_label_set_text(l0, "0");
    lv_obj_center(l0);
    
    lv_obj_t * btnDel = lv_btn_create(numpad_cont);
    lv_obj_set_size(btnDel, btn_w, btn_h);
    lv_obj_set_pos(btnDel, -10 + 2*(btn_w+gap), 3*(btn_h+gap));
    lv_obj_add_style(btnDel, &style_btn_premium, 0);
    lv_obj_set_style_bg_color(btnDel, lv_color_hex(0xAA0000), 0);
    lv_obj_add_event_cb(btnDel, overlay_numpad_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lDel = lv_label_create(btnDel);
    lv_obj_add_style(lDel, &style_btn_text, 0);
    lv_label_set_text(lDel, "DEL");
    lv_obj_center(lDel);

    // CONFIRM button (Centered with respect to columns)
    lv_obj_t * btnConfirm = lv_btn_create(overlay_numpad_container);
    lv_obj_set_size(btnConfirm, 150, 52);
    lv_obj_align(btnConfirm, LV_ALIGN_BOTTOM_RIGHT, -20, -30);
    lv_obj_add_style(btnConfirm, &style_btn_premium, 0);
    lv_obj_set_style_bg_color(btnConfirm, lv_color_hex(0x007700), 0);
    lv_obj_add_event_cb(btnConfirm, overlay_numpad_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lConf = lv_label_create(btnConfirm);
    lv_obj_add_style(lConf, &style_btn_text, 0);
    lv_obj_set_style_text_font(lConf, &lv_font_montserrat_24, 0);
    lv_label_set_text(lConf, "OK");
    lv_obj_center(lConf);
    
    // CANCEL button (Centered with respect to columns)
    lv_obj_t * btnCancel = lv_btn_create(overlay_numpad_container);
    lv_obj_set_size(btnCancel, 150, 52);
    lv_obj_align(btnCancel, LV_ALIGN_BOTTOM_LEFT, 20, -30);
    lv_obj_add_style(btnCancel, &style_btn_premium, 0);
    lv_obj_add_event_cb(btnCancel, overlay_numpad_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lCancel = lv_label_create(btnCancel);
    lv_obj_add_style(lCancel, &style_btn_text, 0);
    lv_obj_set_style_text_font(lCancel, &lv_font_montserrat_24, 0);
    lv_label_set_text(lCancel, "CANCELAR");
    lv_obj_center(lCancel);
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
    lv_obj_set_size(cont_manual, 300, 320); // Reduced from 400
    lv_obj_set_style_bg_opa(cont_manual, 0, 0);
    lv_obj_set_style_border_opa(cont_manual, 0, 0);
    lv_obj_align(cont_manual, LV_ALIGN_CENTER, 0, -80); // Shifted up more
    lv_obj_clear_flag(cont_manual, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * l_man = lv_label_create(cont_manual);
    lv_obj_add_style(l_man, &style_btn_text, 0);
    lv_label_set_text(l_man, "ELEVACION MANUAL");
    lv_obj_align(l_man, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t * btn_up = lv_btn_create(cont_manual);
    lv_obj_set_size(btn_up, 180, 90); // Reduced height
    lv_obj_add_style(btn_up, &style_btn_premium, 0);
    lv_obj_align(btn_up, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_event_cb(btn_up, manual_up_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t * lbl_up = lv_label_create(btn_up);
    lv_obj_add_style(lbl_up, &style_btn_text, 0);
    lv_label_set_text(lbl_up, "SUBIR");
    lv_obj_center(lbl_up);

    lv_obj_t * btn_down = lv_btn_create(cont_manual);
    lv_obj_set_size(btn_down, 180, 90); // Reduced height
    lv_obj_add_style(btn_down, &style_btn_premium, 0);
    lv_obj_align(btn_down, LV_ALIGN_TOP_MID, 0, 150); // Closer to UP (was 180)
    lv_obj_add_event_cb(btn_down, manual_down_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * lbl_down = lv_label_create(btn_down);
    lv_obj_add_style(lbl_down, &style_btn_text, 0);
    lv_label_set_text(lbl_down, "BAJAR");
    lv_obj_center(lbl_down);

    // --- BOTON CALIBRACION COMPLETA ---
    lv_obj_t * btn_cal = lv_btn_create(scr_app_settings);
    lv_obj_set_size(btn_cal, 350, 100); 
    lv_obj_add_style(btn_cal, &style_btn_premium, 0);
    lv_obj_align(btn_cal, LV_ALIGN_CENTER, 360, 180); // Moved to right (aligned with volume)
    lv_obj_add_event_cb(btn_cal, calibration_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * lbl_cal = lv_label_create(btn_cal);
    lv_obj_add_style(lbl_cal, &style_btn_text, 0);
    lv_label_set_text(lbl_cal, "CALIBRACION\nPODOMETRO");
    lv_obj_set_style_text_align(lbl_cal, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl_cal);

    // --- SLIDER DE SENSIBILIDAD ---
    lv_obj_t * cont_sens = lv_obj_create(scr_app_settings);
    lv_obj_set_size(cont_sens, 500, 100);
    lv_obj_set_style_bg_opa(cont_sens, 0, 0);
    lv_obj_set_style_border_opa(cont_sens, 0, 0);
    lv_obj_align(cont_sens, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_clear_flag(cont_sens, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * l_sens = lv_label_create(cont_sens);
    lv_obj_add_style(l_sens, &style_btn_text, 0);
    lv_obj_set_style_text_font(l_sens, &lv_font_montserrat_22, 0);
    lv_label_set_text(l_sens, "SENSIBILIDAD PASOS");
    lv_obj_align(l_sens, LV_ALIGN_TOP_MID, 0, -20);

    slider_sensitivity = lv_slider_create(cont_sens);
    lv_obj_set_size(slider_sensitivity, 400, 20);
    lv_slider_set_range(slider_sensitivity, 0, 100);
    lv_slider_set_value(slider_sensitivity, g_treadmill_state.pedometer_sensitivity, LV_ANIM_OFF);
    lv_obj_align(slider_sensitivity, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_event_cb(slider_sensitivity, sensitivity_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label_sensitivity_pct = lv_label_create(cont_sens);
    lv_obj_add_style(label_sensitivity_pct, &style_btn_text, 0);
    lv_obj_set_style_text_font(label_sensitivity_pct, &lv_font_montserrat_22, 0);
    lv_label_set_text_fmt(label_sensitivity_pct, "%d%%", g_treadmill_state.pedometer_sensitivity);
    lv_obj_align(label_sensitivity_pct, LV_ALIGN_RIGHT_MID, 40, 20);
}

static void create_calibration_screen(void) {
    scr_calibration = lv_obj_create(NULL);
    lv_obj_set_size(scr_calibration, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr_calibration, lv_color_black(), 0);
    lv_obj_clear_flag(scr_calibration, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr_calibration);
    lv_obj_add_style(title, &style_title, 0);
    lv_label_set_text(title, "CALIBRACION PODOMETRO");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    label_cal_speed = lv_label_create(scr_calibration);
    lv_obj_add_style(label_cal_speed, &style_value_extra_large, 0);
    lv_label_set_text(label_cal_speed, "0.0");
    lv_obj_align(label_cal_speed, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *unit = lv_label_create(scr_calibration);
    lv_obj_add_style(unit, &style_unit, 0);
    lv_label_set_text(unit, "km/h");
    lv_obj_align_to(unit, label_cal_speed, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    label_cal_status = lv_label_create(scr_calibration);
    lv_obj_add_style(label_cal_status, &style_btn_text, 0);
    lv_label_set_text(label_cal_status, "Iniciando...");
    lv_obj_set_style_text_color(label_cal_status, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(label_cal_status, LV_ALIGN_CENTER, 0, 120);

    bar_cal_progress = lv_bar_create(scr_calibration);
    lv_obj_set_size(bar_cal_progress, 600, 30);
    lv_obj_align(bar_cal_progress, LV_ALIGN_BOTTOM_MID, 0, -150);
    lv_bar_set_range(bar_cal_progress, 1, 19);
    lv_bar_set_value(bar_cal_progress, 1, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_cal_progress, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_cal_progress, lv_color_hex(0x00FF00), LV_PART_INDICATOR);

    lv_obj_t *btn_cancel = lv_btn_create(scr_calibration);
    lv_obj_set_size(btn_cancel, 250, 80);
    lv_obj_add_style(btn_cancel, &style_btn_premium, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x880000), 0); // Red for cancel
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_event_cb(btn_cancel, calibration_cancel_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_obj_add_style(lbl_cancel, &style_btn_text, 0);
    lv_label_set_text(lbl_cancel, "CANCELAR");
    lv_obj_center(lbl_cancel);
}


void ui_init(void) {
    // 1. Inicializar estado y valores de sesiÃ³n (50% por defecto)
    uint8_t sens_val = 50;
    load_sensitivity_from_nvs(&sens_val);

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.display_brightness = 50;
    g_treadmill_state.audio_volume = 50;
    g_treadmill_state.target_climb_percent = 0.0f;
    g_treadmill_state.total_running_seconds = load_wax_counter_from_nvs();
    g_treadmill_state.cooldown_level = 0;
    g_treadmill_state.is_adjusting_speed = false;
    g_treadmill_state.speed_adjustment_end_ms = 0;
    xSemaphoreGive(g_state_mutex);

    // Aplicar hardware
    bsp_display_brightness_set(50);
    audio_set_volume(50);




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
    create_app_settings_screen(); // Ahora usarÃ¡ los valores de g_treadmill_state que ya son 50
    create_calibration_screen();
    create_series_config_screen();

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

        // --- LÃ“GICA SIMPLIFICADA PARA AJUSTE DELAYED ---
        if (g_treadmill_state.target_speed < 0.5f) {
            // Si la velocidad es < 0.5, la primera pulsaciÃ³n salta a 0.5
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
        // cm_master_set_speed(new_target_speed); // Eliminado: se ejecutarÃ¡ al soltar el botÃ³n
        audio_play_beep();
    }
}

void ui_speed_dec(void) {
    bool should_beep = false;
    float new_target_speed;

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    if (!g_treadmill_state.is_stopped && !g_treadmill_state.is_cooling_down) {
        should_beep = true;

        // --- LÃ“GICA SIMPLIFICADA PARA AJUSTE DELAYED ---
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
        // cm_master_set_speed(new_target_speed); // Eliminado: se ejecutarÃ¡ al soltar el botÃ³n
        audio_play_beep();
    }
}

void ui_speed_execute(void) {
    float target_speed;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    target_speed = g_treadmill_state.target_speed;
    xSemaphoreGive(g_state_mutex);

    cm_master_set_speed(target_speed);
    acoustic_service_set_current_speed(target_speed); // <--- CRÃTICO: Avisar al podÃ³metro
    ESP_LOGI("UI", "ui_speed_execute: Comando de velocidad enviado al motor y podÃ³metro: %.1f km/h", target_speed);
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
        // La velocidad objetivo vuelve a ser la que habÃ­a antes de empezar el cool down
        float resume_speed = g_treadmill_state.speed_before_stop;
        g_treadmill_state.target_speed = resume_speed;
        g_treadmill_state.ramp_mode = RAMP_MODE_COOLDOWN_RESUME;
        
        ESP_LOGI(TAG, "Reanudando desde Cool Down a %.2f km/h (rampa inverter)", resume_speed);
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
        
        ESP_LOGI(TAG, "RESUME activado desde pausa (rampa inverter)");
        xSemaphoreGive(g_state_mutex);
        // La aceleraciÃ³n la gestionarÃ¡ el variador directamente tras la llamada en ui_update_task
    }

    // --- ACTUALIZACIÃ“N DE UI ---
    bsp_display_lock(portMAX_DELAY);
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
        
        // Estilo rojo para el botÃ³n END
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
        // Usamos roundf(x * 2) / 2 para redondear al 0.5 mÃ¡s cercano.
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

        // --- 2. Rampa de inclinaciÃ³n ---
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
        
        // Aplicar bajada de inclinaciÃ³n inmediata (objetivo)
        xSemaphoreGive(g_state_mutex);
        cm_master_set_incline(0.0f);
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    } else {
        // Ya estamos en Cool Down, subir nivel (mÃ¡x 3)
        if (g_treadmill_state.cooldown_level < 3) {
            g_treadmill_state.cooldown_level++;
            ESP_LOGI(TAG, "Cool Down nivel incrementado: %d", g_treadmill_state.cooldown_level);
        }
    }

    int current_level = g_treadmill_state.cooldown_level;
    xSemaphoreGive(g_state_mutex);

    // --- ACTUALIZACIÃ“N DE UI ---
    bsp_display_lock(portMAX_DELAY);
    // BotÃ³n Izquierdo: RESUME
    lv_label_set_text(label_stop_btn, "RESUME");
    lv_obj_set_style_text_align(label_stop_btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_event_cb(btn_stop, stop_resume_event_cb);
    lv_obj_remove_event_cb(btn_stop, end_event_cb);
    lv_obj_add_event_cb(btn_stop, stop_resume_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_local_style_prop(btn_stop, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);

    // BotÃ³n Derecho: COOL DOWN + o END
    if (current_level < 3) {
        lv_label_set_text(label_cooldown_btn, "COOL\nDOWN\n+");
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_add_event_cb(btn_cooldown, cool_down_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(btn_cooldown, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        // Nivel 3 -> BotÃ³n STOP (IdÃ©ntico al STOP normal)
        lv_label_set_text(label_cooldown_btn, "STOP");
        lv_obj_remove_event_cb(btn_cooldown, cool_down_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, end_event_cb);
        lv_obj_remove_event_cb(btn_cooldown, stop_from_cooldown_event_cb);
        lv_obj_add_event_cb(btn_cooldown, stop_from_cooldown_event_cb, LV_EVENT_CLICKED, NULL);
        
        // Estilo normal (gris), quitar el rojo si venÃ­a de un estado anterior (aunque level 3 es nuevo)
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
        bsp_display_lock(portMAX_DELAY);
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
        bsp_display_lock(portMAX_DELAY);
        lv_scr_load(scr_set);
        bsp_display_unlock();
    }
}

static bool _handle_numpad_press_internal(char digit) {
    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT) {
        // Peso: Siempre requiere 2 dÃ­gitos
        if (g_treadmill_state.set_digit_index >= 2) return false;

        // Validar que no exceda el mÃ¡ximo (200)
        char temp_buffer[3];
        strncpy(temp_buffer, g_treadmill_state.set_buffer, g_treadmill_state.set_digit_index);
        temp_buffer[g_treadmill_state.set_digit_index] = digit;
        temp_buffer[g_treadmill_state.set_digit_index + 1] = '\0';

        float proposed_value = atof(temp_buffer);
        if (proposed_value > 200.0f) {
            ESP_LOGI(TAG, "DÃ­gito invÃ¡lido '%c'. El peso propuesto %.0f excede el mÃ¡ximo 200", digit, proposed_value);
            return false;
        }

        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = digit;
        g_treadmill_state.set_digit_index++;
        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = '\0';

        _update_set_display_text_internal();
        return (g_treadmill_state.set_digit_index >= 2);
    } 
    else if (g_treadmill_state.set_mode == SET_MODE_SPEED || g_treadmill_state.set_mode == SET_MODE_CLIMB) {
        // Velocidad e InclinaciÃ³n: LÃ³gica inteligente compartida
        // FORMATO: 0, 2-9 -> Inmediato. 1 -> Espera segundo dÃ­gito.
        
        bool is_climb = (g_treadmill_state.set_mode == SET_MODE_CLIMB);
        float max_val = is_climb ? MAX_CLIMB_PERCENT : MAX_SPEED_KMH;

        if (waiting_for_second_digit) {
            // Ya tenemos el primer dÃ­gito '1', este es el segundo (0-5 para climb, 0-9 para speed)
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
                ESP_LOGI(TAG, "%s %.1f excede el mÃ¡ximo %.1f", is_climb?"Inclinacion":"Velocidad", proposed_value, max_val);
                return false;
            }
            
            strcpy(g_treadmill_state.set_buffer, temp_buffer);
            g_treadmill_state.set_digit_index = 2;
            _update_set_display_text_internal();
            return true;
            
        } else {
            // Primer dÃ­gito
            if (digit == '1') {
                // Esperar segundo dÃ­gito
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
                    ESP_LOGI(TAG, "%s %.1f excede el mÃ¡ximo %.1f", is_climb?"Inclinacion":"Velocidad", val, max_val);
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
    else if (g_treadmill_state.set_mode >= SET_MODE_SERIES_WORK_SPEED && g_treadmill_state.set_mode <= SET_MODE_SERIES_REST_SPM) {
        // Series Configuration: mostly 2 or 3 digits
        int max_digits = 2;
        float max_val = 99.0f;

        if (g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_TIME || 
            g_treadmill_state.set_mode == SET_MODE_SERIES_REST_TIME ||
            g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_SPM ||
            g_treadmill_state.set_mode == SET_MODE_SERIES_REST_SPM) 
        {
            max_digits = 3;
            max_val = 999.0f;
        }

        if (g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_SPEED || g_treadmill_state.set_mode == SET_MODE_SERIES_REST_SPEED) max_val = MAX_SPEED_KMH;
        if (g_treadmill_state.set_mode == SET_MODE_SERIES_WORK_CLIMB || g_treadmill_state.set_mode == SET_MODE_SERIES_REST_CLIMB) max_val = MAX_CLIMB_PERCENT;

        if (g_treadmill_state.set_digit_index >= max_digits) return false;

        // Validar rango propuesto
        char temp[5];
        strncpy(temp, g_treadmill_state.set_buffer, g_treadmill_state.set_digit_index);
        temp[g_treadmill_state.set_digit_index] = digit;
        temp[g_treadmill_state.set_digit_index + 1] = '\0';
        float val = atof(temp);
        if (val > max_val) return false;

        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = digit;
        g_treadmill_state.set_digit_index++;
        g_treadmill_state.set_buffer[g_treadmill_state.set_digit_index] = '\0';

        _update_set_display_text_internal();
        return (g_treadmill_state.set_digit_index >= max_digits);
    }
    return false;
}

bool ui_handle_numpad_press(char digit) {
    audio_play_beep();
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bsp_display_lock(portMAX_DELAY);
    bool result = _handle_numpad_press_internal(digit);
    bsp_display_unlock();
    bool should_confirm = result && (g_treadmill_state.set_mode != SET_MODE_NONE);
    xSemaphoreGive(g_state_mutex);
    
    // Si estÃ¡ completo Y el modo es vÃ¡lido, auto-confirmar
    if (should_confirm) {
        ui_confirm_set_value();
    }
    
    return result;
}

void ui_confirm_set_value(void) {
    // Evitar doble confirmaciÃ³n
    if (confirming_in_progress) {
        ESP_LOGI(TAG, "ui_confirm_set_value: Ya hay una confirmaciÃ³n en progreso, ignorando");
        return;
    }
    confirming_in_progress = true;
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);

    if (g_treadmill_state.set_mode == SET_MODE_WEIGHT) {
        // Para peso, el valor es directo (sin divisiÃ³n por 10)
        float weight = atof(g_treadmill_state.set_buffer);
        if (weight < 30.0f) weight = 30.0f;  // MÃ­nimo 30 kg
        if (weight > 200.0f) weight = 200.0f; // MÃ¡ximo 200 kg
        g_treadmill_state.user_weight_kg = weight;
        // Marcar que el usuario ha introducido el peso
        g_treadmill_state.weight_entered = true;

        // Los botones WEIGHT y BACK se mantienen para permitir correcciÃ³n
        // Solo se cambiarÃ¡n cuando la cinta empiece a moverse

        char weight_msg[80];
        sprintf(weight_msg, "Peso: %dkg - Selecciona una velocidad para comenzar", (int)weight);
        set_info_text_persistent(weight_msg);

        // Marcar que estamos mostrando el peso en el campo de Kcal ANTES de cambiar de pantalla
        showing_weight_in_kcal_field = true;

        // Mostrar el peso en el label de Kcal (pantalla principal) y establecer la unidad "kg" ANTES de _switch_to_main_screen_internal
        bsp_display_lock(portMAX_DELAY);
        lv_label_set_text_fmt(label_kcal, "%d", (int)weight);
        lv_label_set_text(unit_kcal_main, "kg");  // Unidad en pantalla MAIN
        bsp_display_unlock();

        _switch_to_main_screen_internal();
    } else {
        float final_value = 0.0f;
        bool is_speed_mode = false;

        if (g_treadmill_state.set_mode == SET_MODE_SPEED) {
            // Velocidad: 2 dÃ­gitos enteros (ej: "14" = 14.0 km/h)
            final_value = atof(g_treadmill_state.set_buffer);
            if (final_value > MAX_SPEED_KMH) final_value = MAX_SPEED_KMH;
            g_treadmill_state.ramp_mode = RAMP_MODE_NORMAL;
            g_treadmill_state.target_speed = final_value;
            is_speed_mode = true;
        } else if (g_treadmill_state.set_mode == SET_MODE_CLIMB) {
            // InclinaciÃ³n: 2 dÃ­gitos sin dividir (ej: "05" = 5%)
            final_value = atof(g_treadmill_state.set_buffer);
            if (final_value > MAX_CLIMB_PERCENT) final_value = MAX_CLIMB_PERCENT;
            g_treadmill_state.target_climb_percent = final_value;
            is_speed_mode = false;
        } else if (g_treadmill_state.set_mode >= SET_MODE_SERIES_WORK_SPEED && g_treadmill_state.set_mode <= SET_MODE_SERIES_REST_SPM) {
            int val = atoi(g_treadmill_state.set_buffer);
            switch (g_treadmill_state.set_mode) {
                case SET_MODE_SERIES_WORK_SPEED: g_series_config.work.speed = (float)val; break;
                case SET_MODE_SERIES_WORK_CLIMB: g_series_config.work.climb = val; break;
                case SET_MODE_SERIES_WORK_TIME:  g_series_config.work.duration_secs = val; break;
                case SET_MODE_SERIES_WORK_SPM:   g_series_config.work.spm = val; break;
                case SET_MODE_SERIES_REST_SPEED: g_series_config.rest.speed = (float)val; break;
                case SET_MODE_SERIES_REST_CLIMB: g_series_config.rest.climb = val; break;
                case SET_MODE_SERIES_REST_TIME:  g_series_config.rest.duration_secs = val; break;
                case SET_MODE_SERIES_REST_SPM:   g_series_config.rest.spm = val; break;
                default: break;
            }
            xSemaphoreGive(g_state_mutex);
            // Si venimos de la pantalla de configuraciÃ³n de series, volvemos a ella
            if (!scr_series_config) create_series_config_screen();
            
            // Actualizar labels en la pantalla de series
            bsp_display_lock(portMAX_DELAY);
            switch (g_treadmill_state.set_mode) {
                case SET_MODE_SERIES_WORK_SPEED: lv_label_set_text_fmt(label_series_work_speed, "Speed: %.1f", g_series_config.work.speed); break;
                case SET_MODE_SERIES_WORK_CLIMB: lv_label_set_text_fmt(label_series_work_climb, "Incline: %d", g_series_config.work.climb); break;
                case SET_MODE_SERIES_WORK_TIME:  lv_label_set_text_fmt(label_series_work_time, "Time: %d s", g_series_config.work.duration_secs); break;
                case SET_MODE_SERIES_WORK_SPM:   lv_label_set_text_fmt(label_series_work_spm, "SPM: %d", g_series_config.work.spm); break;
                case SET_MODE_SERIES_REST_SPEED: lv_label_set_text_fmt(label_series_rest_speed, "Speed: %.1f", g_series_config.rest.speed); break;
                case SET_MODE_SERIES_REST_CLIMB: lv_label_set_text_fmt(label_series_rest_climb, "Incline: %d", g_series_config.rest.climb); break;
                case SET_MODE_SERIES_REST_TIME:  lv_label_set_text_fmt(label_series_rest_time, "Time: %d s", g_series_config.rest.duration_secs); break;
                case SET_MODE_SERIES_REST_SPM:   lv_label_set_text_fmt(label_series_rest_spm, "SPM: %d", g_series_config.rest.spm); break;
                default: break;
            }
            bsp_display_unlock();

            _switch_to_set_screen_internal(SET_MODE_NONE);
            lv_scr_load(scr_series_config);
            confirming_in_progress = false;
            return;
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
            ESP_LOGI(TAG, "Enviando INCLINACIÃ“N: %.1f %%", final_value);
            cm_master_set_incline(final_value);
        }

        // Actualizar inmediatamente los labels en la pantalla principal
        bsp_display_lock(portMAX_DELAY);
        if (is_speed_mode) {
            // Actualizar velocidad
            int speed_int = (int)final_value;
            int speed_frac = (int)((final_value - speed_int) * 10);
            lv_label_set_text_fmt(label_speed_kmh, "%d.%d", speed_int, speed_frac);

            // Actualizar pace (min:seg por km) - M:SS con lÃ­mite 9:59
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
    
    // Limpiar bandera de confirmaciÃ³n
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

#ifndef SIMULATOR
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
#endif

    lv_scr_load(scr_main);

    // ACTIVAR TRAINING MODE (entrando a pantalla principal despuÃ©s de descarga)
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
    // La actualizaciÃ³n se harÃ¡ en ui_update_task() cuando llegue RSP_STATUS
}

void ui_head_toggle(void) {
    audio_play_beep();

    // Calcular siguiente estado (0->1->2->0)
    uint8_t next_state = (head_value + 1) % 3;

    // Enviar comando al slave (0x01 = HEAD)
    cm_master_set_fan(0x01, next_state);

    // NO actualizar la UI inmediatamente, esperar respuesta del slave
    // La actualizaciÃ³n se harÃ¡ en ui_update_task() cuando llegue RSP_STATUS
}

static void wax_event_cb(lv_event_t *e) {
    audio_play_beep();

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    uint32_t total_seconds = g_treadmill_state.total_running_seconds;
    uint32_t last_wax = g_treadmill_state.last_wax_timestamp;
    double dist = g_treadmill_state.total_running_distance_wax_km;
    xSemaphoreGive(g_state_mutex);

    // 1. Tiempo de uso (H h  M min)
    uint32_t hours = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    lv_label_set_text_fmt(label_wax_usage_time, "%lu h  %lu min", hours, minutes);

    // 2. Distancia (KM)
    lv_label_set_text_fmt(label_wax_dist, "%.0f km", dist);

    // 3. Dias desde WAX (Dias)
    if (last_wax == 0) {
        lv_label_set_text(label_wax_days, "0");
    } else {
        time_t now = time(NULL);
        double diff = difftime(now, (time_t)last_wax);
        int days = (int)(diff / (60 * 60 * 24));
        lv_label_set_text_fmt(label_wax_days, "%d", days);
    }

    lv_scr_load(scr_wax);
    cm_master_set_training_mode(true);
}

static void apply_wax_event_cb(lv_event_t *e) {
    audio_play_beep();

    // Activar la bomba de cera (ON)
    ESP_LOGI(TAG, "Applying wax...");
    cm_master_set_relay(0x01, 1);

    // Esperar exactamente 3 segundos
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Desactivar la bomba de cera (OFF)
    cm_master_set_relay(0x01, 0);
    ESP_LOGI(TAG, "Wax application complete");

    // Resetear los contadores
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.total_running_seconds = 0;
    g_treadmill_state.total_running_distance_wax_km = 0.0;
    g_treadmill_state.last_wax_timestamp = (uint32_t)time(NULL);
    xSemaphoreGive(g_state_mutex);

    // Guardar los contadores reseteados en NVS
    save_wax_counter_to_nvs(0);

    // Actualizar labels en pantalla
    lv_label_set_text(label_wax_usage_time, "0 h  0 min");
    lv_label_set_text(label_wax_dist, "0 km");
    lv_label_set_text(label_wax_days, "0");
}

static void wax_4kmh_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Wax screen: setting speed to 4 km/h");
    cm_master_set_speed(4.0f);
}

static void wax_stop_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "Wax screen: stopping treadmill");
    cm_master_set_speed(0.0f);
}

static void wax_back_event_cb(lv_event_t *e) {
    audio_play_beep();
    cm_master_set_speed(0.0f);
    cm_master_set_training_mode(false);
    lv_scr_load(scr_training_select);
}

void ui_weight_entry(void) {
    ESP_LOGI(TAG, "ui_weight_entry: buttons_are_stop_mode=%d", buttons_are_stop_mode);
    // Verificar si los botones estÃ¡n en modo STOP/COOL DOWN
    if (buttons_are_stop_mode) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        bool is_stopped = g_treadmill_state.is_stopped;
        int cd_level = g_treadmill_state.cooldown_level;
        xSemaphoreGive(g_state_mutex);

        // Si estamos en STOP (Pausa), el botÃ³n derecho es END
        if (is_stopped) {
            ESP_LOGI(TAG, "ui_weight_entry: llamando ui_finish_training() (is_stopped=true)");
            audio_play_beep();
            ui_finish_training();
        } else if (cd_level >= 3) {
            // En Nivel 3 el botÃ³n derecho es STOP (Pausar cinta)
            ESP_LOGI(TAG, "ui_weight_entry: llamando ui_stop_from_cooldown() (level=%d)", cd_level);
            ui_stop_from_cooldown();
        } else {
            ESP_LOGI(TAG, "ui_weight_entry: llamando ui_cool_down()");
            ui_cool_down();
        }
    } else {
        // Actuar como WEIGHT: abrir entrada de peso
        audio_play_beep();
        bsp_display_lock(portMAX_DELAY);
        _switch_to_set_screen_internal(SET_MODE_WEIGHT);
        lv_scr_load(scr_set);
        bsp_display_unlock();
    }
}

void ui_back_to_training(void) {
    // Verificar si los botones estÃ¡n en modo STOP/COOL DOWN
    ESP_LOGI(TAG, "ui_back_to_training ENTRY: buttons_are_stop_mode=%d, tick=%lu",
             buttons_are_stop_mode, (unsigned long)xTaskGetTickCount());
    if (buttons_are_stop_mode) {
        // Actuar como STOP (botÃ³n izquierdo)
        ESP_LOGI(TAG, "ui_back_to_training: llamando ui_stop_resume()");
        ui_stop_resume();
        ESP_LOGI(TAG, "ui_back_to_training: ui_stop_resume() completado");
    } else {
        // Actuar como BACK: volver a selecciÃ³n de entrenamientos
        audio_play_beep();
        bsp_display_lock(portMAX_DELAY);
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
    g_treadmill_state.target_climb_percent = 0.0f; // Resetear inclinaciÃ³n objetivo
    xSemaphoreGive(g_state_mutex);

    // Enviar comando de reset de inclinaciÃ³n al esclavo
    cm_master_set_incline(0.0f);

    // Limpiar timer de WiFi
    bsp_display_lock(portMAX_DELAY);
    if (wifi_check_timer) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
    }
    bsp_display_unlock();

    switch(training_number) {
        case 1:
            ESP_LOGI(TAG, "Entrenamiento libre seleccionado (botÃ³n fÃ­sico)");
            bsp_display_lock(portMAX_DELAY);
            lv_scr_load(scr_main);
            cm_master_set_training_mode(true);  // ACTIVAR TRAINING MODE
            ESP_LOGI(TAG, "Training mode activado (botÃ³n fÃ­sico)");
            set_info_text_persistent("Selecciona una velocidad para comenzar");
            bsp_display_unlock();
            break;
        case 2:
            ESP_LOGI(TAG, "Entrenamiento Itsaso seleccionado (botÃ³n fÃ­sico) - iniciando descarga IA");
            bsp_display_lock(portMAX_DELAY);
            lv_scr_load(scr_loading);  // Pantalla negra durante descarga
            bsp_display_unlock();
            ia_sync_get_next_plan("Itsaso", on_plan_received);
            break;
        case 3:
            ESP_LOGI(TAG, "Entrenamiento Ina seleccionado (botÃ³n fÃ­sico) - iniciando descarga IA");
            bsp_display_lock(portMAX_DELAY);
            lv_scr_load(scr_loading);  // Pantalla negra durante descarga
            bsp_display_unlock();
            ia_sync_get_next_plan("Ina", on_plan_received);
            break;
        default:
            ESP_LOGW(TAG, "NÃºmero de entrenamiento invÃ¡lido: %d", training_number);
            break;
    }
}
static void wifi_selector_event_cb(lv_event_t *e) {
    audio_play_beep();
    ESP_LOGI(TAG, "BotÃ³n WiFi presionado - abriendo lista de redes");
    ui_open_wifi_list();
}

void ui_open_settings(void) {
    audio_play_beep();
    bsp_display_lock(portMAX_DELAY);
    lv_scr_load(scr_app_settings);
    bsp_display_unlock();
}

static void series_config_back_event_cb(lv_event_t *e) {
    audio_play_beep();
    lv_scr_load(scr_training_select);
}

static void series_config_start_event_cb(lv_event_t *e) {
    audio_play_beep();
    
    // Generar ia_plan_t a partir de g_series_config
    ia_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    strcpy(plan.plan_id, "Series_Manual");
    plan.block_count = g_series_config.num_vueltas * 2;
    
    for (int i = 0; i < g_series_config.num_vueltas; i++) {
        // Bloque de TRABAJO
        int w_idx = i * 2;
        plan.blocks[w_idx].target_speed = g_series_config.work.speed;
        plan.blocks[w_idx].target_incline = (float)g_series_config.work.climb;
        plan.blocks[w_idx].primary_cond_type = IA_CONDITION_TIME;
        plan.blocks[w_idx].primary_cond_value = (float)g_series_config.work.duration_secs;
        plan.blocks[w_idx].spm = g_series_config.work.spm;
        plan.blocks[w_idx].steps_enabled = g_series_config.work.steps_enabled;
        plan.blocks[w_idx].sound_enabled = g_series_config.work.sound_enabled;
        snprintf(plan.blocks[w_idx].tramo_label, 63, "SERIE %d/%d", i+1, g_series_config.num_vueltas);
        strcpy(plan.blocks[w_idx].bloque_label, "TRABAJO");

        // Bloque de DESCANSO
        int r_idx = i * 2 + 1;
        plan.blocks[r_idx].target_speed = g_series_config.rest.speed;
        plan.blocks[r_idx].target_incline = (float)g_series_config.rest.climb;
        plan.blocks[r_idx].primary_cond_type = IA_CONDITION_TIME;
        plan.blocks[r_idx].primary_cond_value = (float)g_series_config.rest.duration_secs;
        plan.blocks[r_idx].spm = g_series_config.rest.spm;
        plan.blocks[r_idx].steps_enabled = g_series_config.rest.steps_enabled;
        plan.blocks[r_idx].sound_enabled = g_series_config.rest.sound_enabled;
        snprintf(plan.blocks[r_idx].tramo_label, 63, "SERIE %d/%d", i+1, g_series_config.num_vueltas);
        strcpy(plan.blocks[r_idx].bloque_label, "DESCANSO");
    }
    
    // Cargar plan y empezar
    on_plan_received(&plan, NULL);
    
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_treadmill_state.plan_running = true;
    xSemaphoreGive(g_state_mutex);

    // Mandar comandos iniciales
    cm_master_set_speed(plan.blocks[0].target_speed);
    cm_master_set_incline(plan.blocks[0].target_incline);
    
    // Sincronizar Step Control inicial
    step_control_target_spm = (plan.blocks[0].steps_enabled) ? plan.blocks[0].spm : 0;
    step_control_audio_enabled = plan.blocks[0].sound_enabled;
    update_step_control_timer();
}

static void series_num_inc_cb(lv_event_t *e) {
    audio_play_beep();
    if (g_series_config.num_vueltas < 99) g_series_config.num_vueltas++;
    lv_label_set_text_fmt(label_series_count, "%d", g_series_config.num_vueltas);
}

static void series_num_dec_cb(lv_event_t *e) {
    audio_play_beep();
    if (g_series_config.num_vueltas > 1) g_series_config.num_vueltas--;
    lv_label_set_text_fmt(label_series_count, "%d", g_series_config.num_vueltas);
}

static void series_field_event_cb(lv_event_t *e) {
    set_mode_t mode = (set_mode_t)(uintptr_t)lv_event_get_user_data(e);
    audio_play_beep();
    _show_overlay_numpad(mode);
}

static void series_work_steps_event_cb(lv_event_t *e) {
    g_series_config.work.steps_enabled = !g_series_config.work.steps_enabled;
    audio_play_beep();
    if (g_series_config.work.steps_enabled) {
        lv_obj_set_style_bg_color(btn_series_work_steps, lv_color_hex(0x00AA00), 0);
        lv_label_set_text(label_series_work_steps, "STEPS: ON");
    } else {
        lv_obj_set_style_bg_color(btn_series_work_steps, lv_color_hex(0x444444), 0);
        lv_label_set_text(label_series_work_steps, "STEPS: OFF");
    }
}

static void series_work_sound_event_cb(lv_event_t *e) {
    g_series_config.work.sound_enabled = !g_series_config.work.sound_enabled;
    audio_play_beep();
    if (g_series_config.work.sound_enabled) {
        lv_obj_set_style_bg_color(btn_series_work_sound, lv_color_hex(0x00AA00), 0);
        lv_label_set_text(label_series_work_sound, "SOUND: ON");
    } else {
        lv_obj_set_style_bg_color(btn_series_work_sound, lv_color_hex(0x444444), 0);
        lv_label_set_text(label_series_work_sound, "SOUND: OFF");
    }
}

static void series_rest_steps_event_cb(lv_event_t *e) {
    g_series_config.rest.steps_enabled = !g_series_config.rest.steps_enabled;
    audio_play_beep();
    if (g_series_config.rest.steps_enabled) {
        lv_obj_set_style_bg_color(btn_series_rest_steps, lv_color_hex(0x00AA00), 0);
        lv_label_set_text(label_series_rest_steps, "STEPS: ON");
    } else {
        lv_obj_set_style_bg_color(btn_series_rest_steps, lv_color_hex(0x444444), 0);
        lv_label_set_text(label_series_rest_steps, "STEPS: OFF");
    }
}

static void series_rest_sound_event_cb(lv_event_t *e) {
    g_series_config.rest.sound_enabled = !g_series_config.rest.sound_enabled;
    audio_play_beep();
    if (g_series_config.rest.sound_enabled) {
        lv_obj_set_style_bg_color(btn_series_rest_sound, lv_color_hex(0x00AA00), 0);
        lv_label_set_text(label_series_rest_sound, "SOUND: ON");
    } else {
        lv_obj_set_style_bg_color(btn_series_rest_sound, lv_color_hex(0x444444), 0);
        lv_label_set_text(label_series_rest_sound, "SOUND: OFF");
    }
}

static void create_series_config_screen(void) {
    scr_series_config = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_series_config, lv_color_black(), 0);
    lv_obj_clear_flag(scr_series_config, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(scr_series_config);
    lv_obj_add_style(title, &style_title, 0);
    lv_label_set_text(title, "DEFINIR ENTRENAMIENTO");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    const int col_w = 400;

    // --- COLUMNA IZQUIERDA: SERIES ---
    lv_obj_t * left_col = lv_obj_create(scr_series_config);
    lv_obj_set_size(left_col, 200, 500);
    lv_obj_align(left_col, LV_ALIGN_LEFT_MID, 40, -38);
    lv_obj_set_style_bg_opa(left_col, 0, 0);
    lv_obj_set_style_border_opa(left_col, 0, 0);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(left_col, 30, 0);

    lv_obj_t * l_v_title = lv_label_create(left_col);
    lv_obj_add_style(l_v_title, &style_btn_text, 0);
    lv_label_set_text(l_v_title, "SERIES");

    lv_obj_t * btn_inc = lv_btn_create(left_col);
    lv_obj_set_size(btn_inc, 100, 100);
    lv_obj_add_style(btn_inc, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_inc, series_num_inc_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l_inc = lv_label_create(btn_inc);
    lv_obj_add_style(l_inc, &style_btn_symbol, 0);
    lv_label_set_text(l_inc, LV_SYMBOL_PLUS);
    lv_obj_center(l_inc);

    label_series_count = lv_label_create(left_col);
    lv_obj_add_style(label_series_count, &style_value_main, 0);
    lv_label_set_text_fmt(label_series_count, "%d", g_series_config.num_vueltas);

    lv_obj_t * btn_dec = lv_btn_create(left_col);
    lv_obj_set_size(btn_dec, 100, 100);
    lv_obj_add_style(btn_dec, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_dec, series_num_dec_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l_dec = lv_label_create(btn_dec);
    lv_obj_add_style(l_dec, &style_btn_symbol, 0);
    lv_label_set_text(l_dec, LV_SYMBOL_MINUS);
    lv_obj_center(l_dec);

    // --- COLUMNA CENTRAL: TRABAJO ---
    lv_obj_t * mid_col = lv_obj_create(scr_series_config);
    lv_obj_set_size(mid_col, col_w, 550);
    lv_obj_align(mid_col, LV_ALIGN_CENTER, -100, -18);
    lv_obj_set_style_bg_color(mid_col, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_radius(mid_col, 15, 0);
    lv_obj_set_style_border_color(mid_col, lv_color_hex(0x00AA00), 0);
    lv_obj_set_style_border_width(mid_col, 2, 0);
    lv_obj_set_flex_flow(mid_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mid_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(mid_col, 15, 0);
    lv_obj_set_style_pad_gap(mid_col, 15, 0);

    lv_obj_t * l_work = lv_label_create(mid_col);
    lv_obj_add_style(l_work, &style_btn_text, 0);
    lv_obj_set_style_text_color(l_work, lv_color_hex(0x00FF00), 0);
    lv_label_set_text(l_work, "TRABAJO");

    // Speed
    lv_obj_t * btn_w_speed = lv_btn_create(mid_col);
    lv_obj_set_size(btn_w_speed, 350, 60);
    lv_obj_add_style(btn_w_speed, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_w_speed, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_WORK_SPEED);
    label_series_work_speed = lv_label_create(btn_w_speed);
    lv_obj_add_style(label_series_work_speed, &style_btn_text, 0);
    char speed_buf[32];
    snprintf(speed_buf, sizeof(speed_buf), "Speed: %.1f", g_series_config.work.speed);
    lv_label_set_text(label_series_work_speed, speed_buf);
    lv_obj_center(label_series_work_speed);

    // Incline
    lv_obj_t * btn_w_climb = lv_btn_create(mid_col);
    lv_obj_set_size(btn_w_climb, 350, 60);
    lv_obj_add_style(btn_w_climb, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_w_climb, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_WORK_CLIMB);
    label_series_work_climb = lv_label_create(btn_w_climb);
    lv_obj_add_style(label_series_work_climb, &style_btn_text, 0);
    lv_label_set_text_fmt(label_series_work_climb, "Incline: %d", g_series_config.work.climb);
    lv_obj_center(label_series_work_climb);

    // Time
    lv_obj_t * btn_w_time = lv_btn_create(mid_col);
    lv_obj_set_size(btn_w_time, 350, 60);
    lv_obj_add_style(btn_w_time, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_w_time, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_WORK_TIME);
    label_series_work_time = lv_label_create(btn_w_time);
    lv_obj_add_style(label_series_work_time, &style_btn_text, 0);
    int w_mins = g_series_config.work.duration_secs / 60;
    int w_secs = g_series_config.work.duration_secs % 60;
    lv_label_set_text_fmt(label_series_work_time, "Time: %02d:%02d", w_mins, w_secs);
    lv_obj_center(label_series_work_time);

    // Divider
    lv_obj_t * sep1 = lv_obj_create(mid_col); lv_obj_set_size(sep1, 300, 2); lv_obj_set_style_bg_color(sep1, lv_color_hex(0x444444), 0);

    // Steps Toggle
    btn_series_work_steps = lv_btn_create(mid_col);
    lv_obj_set_size(btn_series_work_steps, 350, 60);
    lv_obj_add_style(btn_series_work_steps, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_series_work_steps, series_work_steps_event_cb, LV_EVENT_CLICKED, NULL);
    label_series_work_steps = lv_label_create(btn_series_work_steps);
    lv_obj_add_style(label_series_work_steps, &style_btn_text, 0);
    lv_label_set_text(label_series_work_steps, g_series_config.work.steps_enabled ? "STEPS: ON" : "STEPS: OFF");
    lv_obj_center(label_series_work_steps);
    if(g_series_config.work.steps_enabled) lv_obj_set_style_bg_color(btn_series_work_steps, lv_color_hex(0x00AA00), 0);

    // SPM
    lv_obj_t * btn_w_spm = lv_btn_create(mid_col);
    lv_obj_set_size(btn_w_spm, 350, 60);
    lv_obj_add_style(btn_w_spm, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_w_spm, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_WORK_SPM);
    label_series_work_spm = lv_label_create(btn_w_spm);
    lv_obj_add_style(label_series_work_spm, &style_btn_text, 0);
    lv_label_set_text_fmt(label_series_work_spm, "SPM: %d", (int)g_series_config.work.spm);
    lv_obj_center(label_series_work_spm);

    // Sound Toggle
    btn_series_work_sound = lv_btn_create(mid_col);
    lv_obj_set_size(btn_series_work_sound, 350, 60);
    lv_obj_add_style(btn_series_work_sound, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_series_work_sound, series_work_sound_event_cb, LV_EVENT_CLICKED, NULL);
    label_series_work_sound = lv_label_create(btn_series_work_sound);
    lv_obj_add_style(label_series_work_sound, &style_btn_text, 0);
    lv_label_set_text(label_series_work_sound, g_series_config.work.sound_enabled ? "SOUND: ON" : "SOUND: OFF");
    lv_obj_center(label_series_work_sound);
    if(g_series_config.work.sound_enabled) lv_obj_set_style_bg_color(btn_series_work_sound, lv_color_hex(0x00AA00), 0);


    // --- COLUMNA DERECHA: DESCANSO ---
    lv_obj_t * right_col = lv_obj_create(scr_series_config);
    lv_obj_set_size(right_col, col_w, 550);
    lv_obj_align(right_col, LV_ALIGN_CENTER, 350, -18);
    lv_obj_set_style_bg_color(right_col, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_radius(right_col, 15, 0);
    lv_obj_set_style_border_color(right_col, lv_color_hex(0xAA0000), 0);
    lv_obj_set_style_border_width(right_col, 2, 0);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(right_col, 15, 0);
    lv_obj_set_style_pad_gap(right_col, 15, 0);

    lv_obj_t * l_rest = lv_label_create(right_col);
    lv_obj_add_style(l_rest, &style_btn_text, 0);
    lv_obj_set_style_text_color(l_rest, lv_color_hex(0xFF0000), 0);
    lv_label_set_text(l_rest, "DESCANSO");

    // Speed
    lv_obj_t * btn_r_speed = lv_btn_create(right_col);
    lv_obj_set_size(btn_r_speed, 350, 60);
    lv_obj_add_style(btn_r_speed, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_r_speed, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_REST_SPEED);
    label_series_rest_speed = lv_label_create(btn_r_speed);
    lv_obj_add_style(label_series_rest_speed, &style_btn_text, 0);
    char rest_speed_buf[32];
    snprintf(rest_speed_buf, sizeof(rest_speed_buf), "Speed: %.1f", g_series_config.rest.speed);
    lv_label_set_text(label_series_rest_speed, rest_speed_buf);
    lv_obj_center(label_series_rest_speed);

    // Incline
    lv_obj_t * btn_r_climb = lv_btn_create(right_col);
    lv_obj_set_size(btn_r_climb, 350, 60);
    lv_obj_add_style(btn_r_climb, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_r_climb, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_REST_CLIMB);
    label_series_rest_climb = lv_label_create(btn_r_climb);
    lv_obj_add_style(label_series_rest_climb, &style_btn_text, 0);
    lv_label_set_text_fmt(label_series_rest_climb, "Incline: %d", g_series_config.rest.climb);
    lv_obj_center(label_series_rest_climb);

    // Time
    lv_obj_t * btn_r_time = lv_btn_create(right_col);
    lv_obj_set_size(btn_r_time, 350, 60);
    lv_obj_add_style(btn_r_time, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_r_time, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_REST_TIME);
    label_series_rest_time = lv_label_create(btn_r_time);
    lv_obj_add_style(label_series_rest_time, &style_btn_text, 0);
    int r_mins = g_series_config.rest.duration_secs / 60;
    int r_secs = g_series_config.rest.duration_secs % 60;
    lv_label_set_text_fmt(label_series_rest_time, "Time: %02d:%02d", r_mins, r_secs);
    lv_obj_center(label_series_rest_time);

    // Divider
    lv_obj_t * sep2 = lv_obj_create(right_col); lv_obj_set_size(sep2, 300, 2); lv_obj_set_style_bg_color(sep2, lv_color_hex(0x444444), 0);

    // Steps Toggle
    btn_series_rest_steps = lv_btn_create(right_col);
    lv_obj_set_size(btn_series_rest_steps, 350, 60);
    lv_obj_add_style(btn_series_rest_steps, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_series_rest_steps, series_rest_steps_event_cb, LV_EVENT_CLICKED, NULL);
    label_series_rest_steps = lv_label_create(btn_series_rest_steps);
    lv_obj_add_style(label_series_rest_steps, &style_btn_text, 0);
    lv_label_set_text(label_series_rest_steps, g_series_config.rest.steps_enabled ? "STEPS: ON" : "STEPS: OFF");
    lv_obj_center(label_series_rest_steps);
    if(g_series_config.rest.steps_enabled) lv_obj_set_style_bg_color(btn_series_rest_steps, lv_color_hex(0x00AA00), 0);

    // SPM
    lv_obj_t * btn_r_spm = lv_btn_create(right_col);
    lv_obj_set_size(btn_r_spm, 350, 60);
    lv_obj_add_style(btn_r_spm, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_r_spm, series_field_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)SET_MODE_SERIES_REST_SPM);
    label_series_rest_spm = lv_label_create(btn_r_spm);
    lv_obj_add_style(label_series_rest_spm, &style_btn_text, 0);
    lv_label_set_text_fmt(label_series_rest_spm, "SPM: %d", (int)g_series_config.rest.spm);
    lv_obj_center(label_series_rest_spm);

    // Sound Toggle
    btn_series_rest_sound = lv_btn_create(right_col);
    lv_obj_set_size(btn_series_rest_sound, 350, 60);
    lv_obj_add_style(btn_series_rest_sound, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_series_rest_sound, series_rest_sound_event_cb, LV_EVENT_CLICKED, NULL);
    label_series_rest_sound = lv_label_create(btn_series_rest_sound);
    lv_obj_add_style(label_series_rest_sound, &style_btn_text, 0);
    lv_label_set_text(label_series_rest_sound, g_series_config.rest.sound_enabled ? "SOUND: ON" : "SOUND: OFF");
    lv_obj_center(label_series_rest_sound);
    if(g_series_config.rest.sound_enabled) lv_obj_set_style_bg_color(btn_series_rest_sound, lv_color_hex(0x00AA00), 0);


    // --- FOOTER BUTTONS ---
    lv_obj_t * btn_back_f = lv_btn_create(scr_series_config);
    lv_obj_set_size(btn_back_f, 300, 80);
    lv_obj_align(btn_back_f, LV_ALIGN_BOTTOM_LEFT, 40, -20);
    lv_obj_add_style(btn_back_f, &style_btn_premium, 0);
    lv_obj_add_event_cb(btn_back_f, series_config_back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l_back_f = lv_label_create(btn_back_f);
    lv_obj_add_style(l_back_f, &style_btn_text, 0);
    lv_label_set_text(l_back_f, "ATRAS");
    lv_obj_center(l_back_f);

    lv_obj_t * btn_start = lv_btn_create(scr_series_config);
    lv_obj_set_size(btn_start, 300, 80);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_RIGHT, -40, -20);
    lv_obj_add_style(btn_start, &style_btn_premium, 0);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x007700), 0);
    lv_obj_add_event_cb(btn_start, series_config_start_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l_start = lv_label_create(btn_start);
    lv_obj_add_style(l_start, &style_btn_text, 0);
    lv_label_set_text(l_start, "COMENZAR");
    lv_obj_center(l_start);
    
    // Create overlay numpad (initially hidden)
    create_overlay_numpad(scr_series_config);
}

//==================================================================================
// SCREEN STATE DETECTION FUNCTIONS
//==================================================================================

bool ui_is_ble_scan_screen_active(void) {
    return (lv_scr_act() == scr_ble_scan);
}

bool ui_is_wax_screen_active(void) {
    return (lv_scr_act() == scr_wax);
}

void ui_open_series_config(void) {
    audio_play_beep();
    
    // Initialize defaults for the configuration if it's the first time
    if (g_series_config.num_vueltas == 0) {
        g_series_config.num_vueltas = 1;
        g_series_config.work.speed = 10.0f;
        g_series_config.work.climb = 0;
        g_series_config.work.duration_secs = 60;
        g_series_config.work.steps_enabled = false;
        g_series_config.work.spm = 180;
        g_series_config.work.sound_enabled = false;
        
        g_series_config.rest.speed = 5.0f;
        g_series_config.rest.climb = 0;
        g_series_config.rest.duration_secs = 30;
        g_series_config.rest.steps_enabled = false;
        g_series_config.rest.spm = 120;
        g_series_config.rest.sound_enabled = false;
    }

    if (!scr_series_config) {
        create_series_config_screen();
    } else {
        // Update the label if screen already exists
        lv_label_set_text_fmt(label_series_count, "%d", g_series_config.num_vueltas);
    }
    lv_scr_load(scr_series_config);
}
