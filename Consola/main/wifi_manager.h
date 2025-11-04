#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>

#define WIFI_MANAGER_MAX_NETWORKS 20
#define WIFI_MANAGER_MAX_SSID_LEN 32
#define WIFI_MANAGER_MAX_PASSWORD_LEN 64

/**
 * @brief Structure to hold scanned WiFi network information
 */
typedef struct {
    char ssid[WIFI_MANAGER_MAX_SSID_LEN];
    int8_t rssi;
    wifi_auth_mode_t auth_mode;
} wifi_network_info_t;

/**
 * @brief Initialize the WiFi manager module
 *
 * This function initializes the NVS namespace for storing WiFi credentials
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Scan for available WiFi networks
 *
 * @param networks Pointer to array to store network information
 * @param max_networks Maximum number of networks to return
 * @param num_found Pointer to store actual number of networks found
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_scan_networks(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found);

/**
 * @brief Save WiFi credentials to NVS
 *
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Load WiFi credentials from NVS
 *
 * @param ssid Network SSID to search for
 * @param password Buffer to store retrieved password (must be at least WIFI_MANAGER_MAX_PASSWORD_LEN bytes)
 * @return ESP_OK if credentials found, ESP_ERR_NVS_NOT_FOUND if not found, other error codes on failure
 */
esp_err_t wifi_manager_load_credentials(const char *ssid, char *password);

/**
 * @brief Check if credentials exist for a given SSID
 *
 * @param ssid Network SSID to check
 * @return true if credentials exist, false otherwise
 */
bool wifi_manager_has_credentials(const char *ssid);

/**
 * @brief Delete stored credentials for a given SSID
 *
 * @param ssid Network SSID
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_delete_credentials(const char *ssid);

/**
 * @brief Get the currently connected SSID
 *
 * @param ssid Buffer to store SSID (must be at least WIFI_MANAGER_MAX_SSID_LEN bytes)
 * @return ESP_OK if connected, error code otherwise
 */
esp_err_t wifi_manager_get_current_ssid(char *ssid);

/**
 * @brief Set the last successfully connected SSID to prioritize it in the future
 * 
 * @param ssid The SSID of the network that was successfully connected
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_set_last_connected(const char *ssid);

/**
 * @brief Get all saved SSIDs from NVS, ordered by most recently used
 *
 * @param networks Pointer to array to store network information (only SSID will be filled)
 * @param max_networks Maximum number of networks to return
 * @param num_found Pointer to store actual number of networks found
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_saved_ssids_ordered(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found);

/**
 * @brief Get all saved SSIDs from NVS
 *
 * @param networks Pointer to array to store network information (only SSID will be filled)
 * @param max_networks Maximum number of networks to return
 * @param num_found Pointer to store actual number of networks found
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_saved_ssids(wifi_network_info_t *networks, uint16_t max_networks, uint16_t *num_found);

#endif // WIFI_MANAGER_H
