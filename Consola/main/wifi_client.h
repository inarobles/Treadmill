#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Initializes the WiFi client, configures it as a station, and starts the connection process.
 *
 * This function sets up the entire WiFi stack for station mode using esp-hosted.
 * It initializes the underlying TCP/IP stack, the event loop, and the WiFi driver.
 * It then configures the ESP32-P4 to connect to a predefined AP and starts the WiFi.
 *
 * NOTE: File download is NOT started automatically to prevent display interference.
 * Call wifi_download_file() manually when ready.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t wifi_client_init(void);

/**
 * @brief Manually trigger file download from server
 *
 * This function should be called ONLY when the display is stable and you're
 * ready to tolerate potential visual artifacts during the HTTPS download.
 * The download generates many cache sync errors that can cause temporary
 * black zones on the display.
 *
 * WARNING: Do not call this while display is actively rendering important UI.
 */
void wifi_download_file(const char *url);

/**
 * @brief Global pointer to downloaded file content (null-terminated string)
 *
 * After successful download, this points to the file content in memory.
 * The memory is allocated and owned by the wifi_client module.
 * Do not free this pointer.
 */
extern char *g_downloaded_file_content;

/**
 * @brief Size of the downloaded file in bytes (excluding null terminator)
 */
extern int g_downloaded_file_size;

/**
 * @brief Mutex for protecting access to download buffer
 *
 * IMPORTANT: Always acquire this mutex before accessing g_downloaded_file_content
 * or g_downloaded_file_size to prevent race conditions between download task and UI.
 */
extern SemaphoreHandle_t g_download_mutex;

/**
 * @brief Triggers an asynchronous upload for Ina's training file.
 */
void upload_to_ina(int number);

/**
 * @brief Triggers an asynchronous upload for Itsaso's training file.
 */
void upload_to_itsaso(int number);

/**
 * @brief Uploads text data to Ina's training file.
 */
void upload_text_to_ina(const char *text);

/**
 * @brief Uploads text data to Itsaso's training file.
 */
void upload_text_to_itsaso(const char *text);

/**
 * @brief Uploads training data to Oracle Cloud for Ina.
 */
void upload_to_oracle_ina(const char *text);

/**
 * @brief Uploads training data to Oracle Cloud for Itsaso.
 */
void upload_to_oracle_itsaso(const char *text);

/**
 * @brief Downloads a training plan for a specific user.
 *
 * @param username The user for whom to download the plan.
 */
void wifi_download_plan(const char* username);

/**
 * @brief Checks if the WiFi is currently connected and has an IP address.
 *
 * @return true if connected, false otherwise.
 */
bool is_wifi_connected(void);

bool is_internet_connected(void);

void check_internet_connectivity(void);

extern bool g_internet_connected;

/**
 * @brief Connect to a WiFi network with given credentials
 *
 * This function disconnects from current network (if any) and connects to the specified network.
 *
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t wifi_client_connect(const char *ssid, const char *password);

/**
 * @brief Sends data via HTTP POST to a specific PHP script with multiple headers to simulate a browser.
 *
 * @param filename The name of the file to be used in the URL.
 * @param datos_a_enviar The string data to be sent in the POST body.
 */
void subirDatosPOST_Avanzado(const char* filename, const char* datos_a_enviar);


#endif // WIFI_CLIENT_H
