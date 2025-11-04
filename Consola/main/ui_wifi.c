#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"
#include "audio.h"
#include "wifi_client.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <string.h>

// Extern variables from ui.c
extern const char *TAG;
extern lv_obj_t *scr_training_select;

// --- Custom Event ---
static uint32_t LV_EVENT_WIFI_SCAN_DONE;

// --- Global variables for WiFi screens ---
static lv_obj_t *scr_wifi_password;
static lv_obj_t *wifi_password_textarea;
static lv_obj_t *wifi_password_ok_btn;

static char ssid_to_connect[WIFI_MANAGER_MAX_SSID_LEN];
static char ssid_to_edit[WIFI_MANAGER_MAX_SSID_LEN];

static wifi_network_info_t g_scanned_networks[WIFI_MANAGER_MAX_NETWORKS];
static uint16_t g_num_scanned_networks = 0;

// --- Forward declarations ---
void ui_open_wifi_list(void);
static void build_wifi_list(void);
static void loading_screen_event_cb(lv_event_t *e);
static void wifi_scan_task(void *pvParameters);
static void saved_network_delete_cb(lv_event_t *e);
static void saved_network_edit_cb(lv_event_t *e);
static void wifi_password_keyboard_edit_ok_cb(lv_event_t *e);
static void wifi_password_keyboard_connect_cb(lv_event_t *e);
static void wifi_network_connect_cb(lv_event_t *e);
static void back_to_training_select_from_wifi_cb(lv_event_t *e);

// --- Event Handlers ---

static void back_to_training_select_from_wifi_cb(lv_event_t *e) {
    audio_play_beep();
    lv_scr_load(scr_training_select);
}

static void saved_network_delete_cb(lv_event_t *e) {
    audio_play_beep();
    const char* ssid = (const char*)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Deleting WiFi credentials for %s", ssid);
    wifi_manager_delete_credentials(ssid);
    ui_open_wifi_list(); // Refresh the list
}

static void saved_network_edit_cb(lv_event_t *e) {
    audio_play_beep();
    const char* ssid = (const char*)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Editing WiFi credentials for %s", ssid);

    strncpy(ssid_to_edit, ssid, sizeof(ssid_to_edit) - 1);
    ssid_to_edit[sizeof(ssid_to_edit) - 1] = '\0';

    lv_textarea_set_text(wifi_password_textarea, "");
    lv_textarea_set_placeholder_text(wifi_password_textarea, ssid);
    
    lv_obj_remove_event_cb(wifi_password_ok_btn, NULL);
    lv_obj_add_event_cb(wifi_password_ok_btn, wifi_password_keyboard_edit_ok_cb, LV_EVENT_CLICKED, NULL);

    lv_scr_load(scr_wifi_password);
}

static void wifi_password_keyboard_edit_ok_cb(lv_event_t *e) {
    audio_play_beep();
    const char *password = lv_textarea_get_text(wifi_password_textarea);

    if (password && strlen(password) > 0) {
        ESP_LOGI(TAG, "Saving new password for: %s", ssid_to_edit);
        wifi_manager_save_credentials(ssid_to_edit, password);
    } else {
        ESP_LOGW(TAG, "Password is empty, aborting edit.");
    }
    ui_open_wifi_list();
}

static void wifi_password_keyboard_connect_cb(lv_event_t *e) {
    audio_play_beep();
    const char *password = lv_textarea_get_text(wifi_password_textarea);

    if (password && strlen(password) > 0) {
        ESP_LOGI(TAG, "Attempting to connect to %s", ssid_to_connect);
        wifi_manager_save_credentials(ssid_to_connect, password);
        wifi_client_connect(ssid_to_connect, password);
    } else {
        ESP_LOGW(TAG, "Password is empty, aborting connection.");
    }
    lv_scr_load(scr_training_select); // Go back to main menu after attempting connection
}

static void wifi_network_connect_cb(lv_event_t *e) {
    audio_play_beep();
    wifi_network_info_t *info = (wifi_network_info_t *)lv_event_get_user_data(e);

    if (info->auth_mode == WIFI_AUTH_OPEN) {
        ESP_LOGI(TAG, "Connecting to open network: %s", info->ssid);
        wifi_client_connect(info->ssid, NULL);
        lv_scr_load(scr_training_select);
        return;
    }

    char password[WIFI_MANAGER_MAX_PASSWORD_LEN];
    if (wifi_manager_load_credentials(info->ssid, password) == ESP_OK) {
        ESP_LOGI(TAG, "Connecting to saved network: %s", info->ssid);
        wifi_client_connect(info->ssid, password);
        lv_scr_load(scr_training_select);
    } else {
        ESP_LOGI(TAG, "Password required for network: %s", info->ssid);
        strncpy(ssid_to_connect, info->ssid, sizeof(ssid_to_connect) - 1);
        ssid_to_connect[sizeof(ssid_to_connect) - 1] = '\0';

        lv_textarea_set_text(wifi_password_textarea, "");
        lv_textarea_set_placeholder_text(wifi_password_textarea, info->ssid);
        
        lv_obj_remove_event_cb(wifi_password_ok_btn, NULL);
        lv_obj_add_event_cb(wifi_password_ok_btn, wifi_password_keyboard_connect_cb, LV_EVENT_CLICKED, NULL);

        lv_scr_load(scr_wifi_password);
    }
}

// --- UI Creation ---

void create_wifi_screens(void) {
    LV_EVENT_WIFI_SCAN_DONE = lv_event_register_id();

    // Password Entry Screen
    scr_wifi_password = lv_obj_create(NULL);
    lv_obj_set_size(scr_wifi_password, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr_wifi_password, lv_color_black(), 0);

    wifi_password_textarea = lv_textarea_create(scr_wifi_password);
    lv_obj_set_size(wifi_password_textarea, LV_PCT(90), 60);
    lv_obj_align(wifi_password_textarea, LV_ALIGN_TOP_MID, 0, 20);
    lv_textarea_set_password_mode(wifi_password_textarea, true);
    lv_textarea_set_one_line(wifi_password_textarea, true);
    lv_obj_set_style_text_font(wifi_password_textarea, &lv_font_montserrat_20, 0);

    lv_obj_t *kb = lv_keyboard_create(scr_wifi_password);
    lv_obj_set_size(kb, LV_PCT(100), LV_PCT(50));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, wifi_password_textarea);

    wifi_password_ok_btn = lv_btn_create(scr_wifi_password);
    lv_obj_set_size(wifi_password_ok_btn, 150, 50);
    lv_obj_align(wifi_password_ok_btn, LV_ALIGN_TOP_RIGHT, -10, 90);
    lv_obj_t *label_ok = lv_label_create(wifi_password_ok_btn);
    lv_label_set_text(label_ok, "GUARDAR");
    lv_obj_center(label_ok);
}

static void create_section_title(lv_obj_t *parent, const char *title) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_pad_top(label, 20, 0);
    lv_obj_set_style_pad_bottom(label, 10, 0);
}

static void build_wifi_list(void) {
    lv_obj_t *scr_wifi_list = lv_obj_create(NULL);
    lv_obj_set_size(scr_wifi_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr_wifi_list, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr_wifi_list);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_text(title, "Redes WiFi");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *main_container = lv_obj_create(scr_wifi_list);
    lv_obj_set_size(main_container, LV_PCT(95), LV_PCT(80));
    lv_obj_align(main_container, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(main_container, 10, 0);
    lv_obj_set_style_pad_row(main_container, 10, 0);
    lv_obj_set_scrollbar_mode(main_container, LV_SCROLLBAR_MODE_AUTO);

    // --- Data Gathering ---
    char current_ssid[WIFI_MANAGER_MAX_SSID_LEN] = {0};
    bool is_connected = is_wifi_connected();
    wifi_ap_record_t ap_info;
    if (is_connected) {
        esp_wifi_sta_get_ap_info(&ap_info);
        strlcpy(current_ssid, (char *)ap_info.ssid, sizeof(current_ssid));
    }

    wifi_network_info_t saved_networks[WIFI_MANAGER_MAX_NETWORKS];
    uint16_t num_saved_networks = 0;
    wifi_manager_get_saved_ssids(saved_networks, WIFI_MANAGER_MAX_NETWORKS, &num_saved_networks);

    // --- Filter Scanned Networks ---
    wifi_network_info_t filtered_networks[WIFI_MANAGER_MAX_NETWORKS];
    uint16_t num_filtered_networks = 0;
    for (int i = 0; i < g_num_scanned_networks; i++) {
        bool found = false;
        for (int j = 0; j < num_filtered_networks; j++) {
            if (strcmp(g_scanned_networks[i].ssid, filtered_networks[j].ssid) == 0) {
                found = true;
                if (g_scanned_networks[i].rssi > filtered_networks[j].rssi) {
                    filtered_networks[j] = g_scanned_networks[i];
                }
                break;
            }
        }
        if (!found) {
            filtered_networks[num_filtered_networks++] = g_scanned_networks[i];
        }
    }

    // --- UI Building ---

    // 1. Connected Network
    if (is_connected) {
        lv_obj_t *item = lv_obj_create(main_container);
        lv_obj_set_size(item, LV_PCT(100), 80);
        lv_obj_set_style_bg_color(item, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text_fmt(label, "Conectado a: %s (%d)", current_ssid, ap_info.rssi);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }

    // 2. Available Networks
    create_section_title(main_container, "Redes Disponibles:");
    bool has_available = false;
    for (int i = 0; i < num_filtered_networks; i++) {
        if (strcmp(current_ssid, filtered_networks[i].ssid) != 0) {
            has_available = true;

            lv_obj_t *item = lv_obj_create(main_container);
            lv_obj_set_size(item, LV_PCT(100), 80);
            lv_obj_set_layout(item, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *ssid_label = lv_label_create(item);
            lv_label_set_text_fmt(ssid_label, "%s (%d)", filtered_networks[i].ssid, filtered_networks[i].rssi);
            lv_obj_set_flex_grow(ssid_label, 1);

            lv_obj_t *btn_connect = lv_btn_create(item);
            lv_obj_add_event_cb(btn_connect, wifi_network_connect_cb, LV_EVENT_CLICKED, &filtered_networks[i]);
            lv_obj_t *label_connect = lv_label_create(btn_connect);
            lv_label_set_text(label_connect, "Conectar");

            bool is_saved = false;
            for (int j = 0; j < num_saved_networks; j++) {
                if (strcmp(filtered_networks[i].ssid, saved_networks[j].ssid) == 0) {
                    is_saved = true;
                    break;
                }
            }

            if (is_saved) {
                lv_obj_t *btn_edit = lv_btn_create(item);
                lv_obj_add_event_cb(btn_edit, saved_network_edit_cb, LV_EVENT_CLICKED, (void*)filtered_networks[i].ssid);
                lv_obj_t *label_edit = lv_label_create(btn_edit);
                lv_label_set_text(label_edit, "Editar");

                lv_obj_t *btn_del = lv_btn_create(item);
                lv_obj_set_style_bg_color(btn_del, lv_palette_main(LV_PALETTE_RED), 0);
                lv_obj_add_event_cb(btn_del, saved_network_delete_cb, LV_EVENT_CLICKED, (void*)filtered_networks[i].ssid);
                lv_obj_t *label_del = lv_label_create(btn_del);
                lv_label_set_text(label_del, "Borrar");
            }
        }
    }
    if (!has_available) {
        lv_obj_t *label = lv_label_create(main_container);
        lv_label_set_text(label, "No se encontraron redes.");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }

    // 3. Saved Networks
    create_section_title(main_container, "Redes Guardadas:");
    bool has_saved = false;
    for (int i = 0; i < num_saved_networks; i++) {
        bool is_scanned = false;
        for (int j = 0; j < num_filtered_networks; j++) {
            if (strcmp(saved_networks[i].ssid, filtered_networks[j].ssid) == 0) {
                is_scanned = true;
                break;
            }
        }
        if (strcmp(current_ssid, saved_networks[i].ssid) != 0 && !is_scanned) {
            has_saved = true;
            lv_obj_t *item = lv_obj_create(main_container);
            lv_obj_set_size(item, LV_PCT(100), 80);
            lv_obj_set_layout(item, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *ssid_label = lv_label_create(item);
            lv_label_set_text(ssid_label, saved_networks[i].ssid);
            lv_obj_set_flex_grow(ssid_label, 1);

            lv_obj_t *btn_edit = lv_btn_create(item);
            lv_obj_add_event_cb(btn_edit, saved_network_edit_cb, LV_EVENT_CLICKED, (void*)saved_networks[i].ssid);
            lv_obj_t *label_edit = lv_label_create(btn_edit);
            lv_label_set_text(label_edit, "Editar");

            lv_obj_t *btn_del = lv_btn_create(item);
            lv_obj_set_style_bg_color(btn_del, lv_palette_main(LV_PALETTE_RED), 0);
            lv_obj_add_event_cb(btn_del, saved_network_delete_cb, LV_EVENT_CLICKED, (void*)saved_networks[i].ssid);
            lv_obj_t *label_del = lv_label_create(btn_del);
            lv_label_set_text(label_del, "Borrar");
        }
    }
    if (!has_saved) {
        lv_obj_t *label = lv_label_create(main_container);
        lv_label_set_text(label, "No hay otras redes guardadas.");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }

    lv_obj_t *btn_back = lv_btn_create(scr_wifi_list);
    lv_obj_set_size(btn_back, 150, 50);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(btn_back, back_to_training_select_from_wifi_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "Volver");
    lv_obj_center(label_back);

    lv_scr_load(scr_wifi_list);
}

static void loading_screen_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_WIFI_SCAN_DONE) {
        build_wifi_list();
    }
}

static void wifi_scan_task(void *pvParameters) {
    lv_obj_t *scr_loading = (lv_obj_t *)pvParameters;

    wifi_manager_scan_networks(g_scanned_networks, WIFI_MANAGER_MAX_NETWORKS, &g_num_scanned_networks);

    lv_event_send(scr_loading, LV_EVENT_WIFI_SCAN_DONE, NULL);

    vTaskDelete(NULL);
}

void ui_open_wifi_list(void) {
    // Create a loading screen
    lv_obj_t *scr_loading = lv_obj_create(NULL);
    lv_obj_set_size(scr_loading, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr_loading, lv_color_black(), 0);
    lv_obj_add_event_cb(scr_loading, loading_screen_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *spinner = lv_spinner_create(scr_loading, 1000, 60);
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_center(spinner);

    lv_obj_t *label = lv_label_create(scr_loading);
    lv_label_set_text(label, "Escaneando redes wifi, espere un momento.");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_align_to(label, spinner, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    lv_scr_load(scr_loading);

    // Create a task to perform the scan
    xTaskCreate(wifi_scan_task, "wifi_scan_task", 4096, scr_loading, 5, NULL);
}
