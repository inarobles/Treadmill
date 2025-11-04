#include "treadmill_state.h"

TreadmillState g_treadmill_state = {
    .speed_kmh = 0.0f,
    .climb_percent = 0.0f,
    .elapsed_seconds = 0,
    .total_distance_km = 0.0,
    .is_stopped = false,
    .is_cooling_down = false,
    .is_resuming = false,
    .resume_from_stop = false,
    .speed_before_stop = 0.0f,
    .target_speed = 0.0f,
    .cooldown_climb_ramp_rate = 0.0f,
    .real_pulse = 0,
    .ble_connected = false,
    .sim_pulse = 80,
    .sim_kcal = 0.0f,
    .set_mode = SET_MODE_NONE,
    .set_buffer = {0},
    .set_digit_index = 0,
    .blink_timer = NULL,
    .blink_state = false,
    .ramp_mode = RAMP_MODE_NORMAL,
    .selected_training = 1,
    .has_run_minimum_time = false,
    .has_uploaded = false,
    .has_shown_welcome_message = false,
    .user_weight_kg = DEFAULT_USER_WEIGHT_KG,
    .weight_entered = false,
};

SemaphoreHandle_t g_state_mutex = NULL;
