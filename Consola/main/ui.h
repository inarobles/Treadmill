#ifndef UI_H
#define UI_H

#include "treadmill_state.h"

// Initialization
void ui_init(void);

// UI Task
void ui_update_task(void *pvParameter);

// Functions for button/event handling
void ui_speed_inc(void);
void ui_speed_dec(void);
void ui_climb_inc(void);
void ui_climb_dec(void);
void ui_stop_resume(void);
void ui_cool_down(void);
void ui_set_speed(void);
void ui_set_climb(void);
bool ui_handle_numpad_press(char digit);
void ui_confirm_set_value(void);
void ui_switch_to_main_screen_from_timer(void);

bool ui_is_main_screen_active(void);
bool ui_is_training_select_screen_active(void);

// Function to switch to main screen after download completes
void ui_loading_complete(void);

// Function to switch to main screen after upload completes
void ui_upload_complete(bool success);

// Functions for training selection buttons (physical buttons)
void ui_select_training(int training_number);

// Functions for CHEST and HEAD buttons
void ui_chest_toggle(void);
void ui_head_toggle(void);
void ui_weight_entry(void);
void ui_back_to_training(void);

// --- New WiFi UI Functions ---
// Defined in ui_wifi.c
void create_wifi_screens(void);
void ui_open_wifi_list(void);

#endif // UI_H
