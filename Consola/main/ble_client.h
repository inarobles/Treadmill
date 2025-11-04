#ifndef BLE_CLIENT_H
#define BLE_CLIENT_H

#include <stdint.h>
#include "host/ble_hs.h" // For ble_addr_t and other NimBLE types

// Callback function type to notify the UI about a found device
typedef void (*ble_device_found_callback_t)(const char* name, ble_addr_t addr);

/**
 * @brief Initializes the BLE client for ESP-Hosted mode.
 *        This function sets up the NimBLE host and starts the BLE host task.
 *        It assumes that NVS and the ESP-Hosted transport layer have been initialized beforehand.
 */
void ble_client_init(void);

/**
 * @brief Starts a new BLE scan for devices advertising the Heart Rate service.
 *
 * @param cb The callback function to be invoked for each device found.
 */
void ble_client_start_scan(ble_device_found_callback_t cb);

/**
 * @brief Connects to a specific BLE device using its address.
 *
 * @param addr The address of the device to connect to.
 */
void ble_client_connect(ble_addr_t addr);

/**
 * @brief Saves the address of a BLE device to Non-Volatile Storage (NVS).
 *
 * @param addr The address of the device to save.
 */
void ble_client_save_device(ble_addr_t addr);

/**
 * @brief Loads a previously saved BLE device address from NVS.
 *
 * @param addr Pointer to a ble_addr_t struct to store the loaded address.
 * @return true if an address was successfully loaded, false otherwise.
 */
bool ble_client_load_saved_device(ble_addr_t *addr);


#endif // BLE_CLIENT_H