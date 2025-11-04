#ifndef TREADMILL_STATE_H
#define TREADMILL_STATE_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#define MAX_SPEED_KMH 19.5f
#define MAX_CLIMB_PERCENT 15.0f
#define DEFAULT_USER_WEIGHT_KG 70.0f  // Peso por defecto del usuario en kg

typedef enum {
    SET_MODE_NONE,
    SET_MODE_SPEED,
    SET_MODE_CLIMB,
    SET_MODE_WEIGHT
} set_mode_t;

typedef enum {
    RAMP_MODE_NORMAL,
    RAMP_MODE_STOP_STOP,
    RAMP_MODE_COOLDOWN_STOP,
    RAMP_MODE_STOP_RESUME,
    RAMP_MODE_COOLDOWN_RESUME,
} ramp_mode_t;

typedef struct {
    float speed_kmh;
    float climb_percent;
    float target_climb_percent; // Nueva variable para la inclinación objetivo
    uint32_t elapsed_seconds;
    double total_distance_km;
    bool is_stopped;
    bool is_cooling_down;
    bool is_resuming;
    bool resume_from_stop;
    float speed_before_stop;
    float target_speed;
    float cooldown_climb_ramp_rate;

    // Data from BLE Heart Rate monitor
    volatile uint16_t real_pulse;
    volatile bool ble_connected;

    // Simulated data (fallback)
    volatile int sim_pulse;
    volatile float sim_kcal;
    set_mode_t set_mode;
    char set_buffer[4];
    int set_digit_index;
    lv_timer_t *blink_timer;
    bool blink_state;
    ramp_mode_t ramp_mode;

    // Training type (1=Free, 2=Itsaso, 3=Ina, 4=Alain, 5=Urko)
    int selected_training;

    // Training completion tracking
    bool has_run_minimum_time;  // true if treadmill ran for at least 10 seconds
    bool has_uploaded;           // true if training data has been uploaded successfully
    bool has_shown_welcome_message; // true if the initial welcome message has been shown

    // User weight
    float user_weight_kg;        // Weight of the user in kg
    bool weight_entered;         // true if user has entered their weight

    // Wax maintenance tracking
    uint32_t total_running_seconds;  // Total accumulated running time (for wax counter)
} TreadmillState;

extern TreadmillState g_treadmill_state;
extern SemaphoreHandle_t g_state_mutex;

#endif // TREADMILL_STATE_H
