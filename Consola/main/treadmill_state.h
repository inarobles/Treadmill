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
    int cooldown_level;              // 0: none, 1-4: specific ramp rates

    // Data from BLE Heart Rate monitor
    volatile uint16_t real_pulse;
    volatile bool ble_connected;

    // Steps/Cadence (from Acoustic/Microphone Service)
    uint32_t steps;
    float cadence;

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
    uint32_t total_running_seconds;      // Total accumulated running time (for wax counter)
    uint32_t last_wax_timestamp;         // UTC timestamp of the last WAX application
    double total_running_distance_wax_km; // Total distance since last WAX

    // AI Plan Execution
    bool plan_running;              // true if an IA plan is being executed
    int current_block_idx;          // index of the current block in the plan
    uint32_t block_elapsed_seconds;  // seconds elapsed in the current block
    double block_distance_km;       // distance accumulated in current block
    float block_kcal;               // kcal accumulated in current block

    // Speed Adjustment logic
    bool is_adjusting_speed;         // true while SPEED button is held
    uint32_t speed_adjustment_end_ms; // timestamp of button release (grace period)

    // App Settings
    uint8_t display_brightness;     // 0-100
    uint8_t audio_volume;           // 0-100
    uint8_t pedometer_sensitivity;  // 0-100
    uint32_t stride;                // Current stride value
} TreadmillState;


extern TreadmillState g_treadmill_state;
extern SemaphoreHandle_t g_state_mutex;

#endif // TREADMILL_STATE_H
