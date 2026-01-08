#include "imu_service.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>
#include "bsp/esp32_p4_function_ev_board.h"
#include "bmi270_config.h"

static const char *TAG = "IMU_SERVICE";

// I2C Configuration (FPC3 Connector Pins 11 & 12 -> GPIO 8 & 7)
#define I2C_MASTER_SCL_IO           8       // Pin 11 FPC3 (ES_I2C_SCL)
#define I2C_MASTER_SDA_IO           7       // Pin 12 FPC3 (ES_I2C_SDA)
#define I2C_MASTER_FREQ_HZ          100000  // Lowered to 100kHz for ribbon cable stability
#define IMU_INT_GPIO                2       // Pin 10 FPC3 (INT1)

static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static i2c_master_dev_handle_t s_bmi270_dev_handle = NULL;

// BMI270 Registers
#define BMI270_ADDR                 0x68
#define BMI270_REG_CHIP_ID          0x00
#define BMI270_REG_INTERNAL_STATUS  0x21
#define BMI270_REG_ACC_DATA         0x0C
#define BMI270_REG_INIT_CTRL        0x59
#define BMI270_REG_INIT_ADDR_0      0x5B
#define BMI270_REG_INIT_DATA        0x5E
#define BMI270_REG_PWR_CONF         0x7C
#define BMI270_REG_PWR_CTRL         0x7D
#define BMI270_REG_ACC_CONF         0x40
#define BMI270_REG_ACC_RANGE        0x41
#define BMI270_RANGE_2G             0x00
#define BMI270_RANGE_4G             0x01
#define BMI270_RANGE_8G             0x02
#define BMI270_RANGE_16G            0x03

// Algorithm Constants
#define SAMPLE_RATE_HZ              200
static float s_shock_threshold = 1.2f; 
#define DEBOUNCE_MS                 250

static uint32_t s_step_count = 0;
static uint32_t s_int_count = 0;
static float s_cadence = 0.0f;
static int64_t s_last_step_time = 0;
static SemaphoreHandle_t s_imu_mutex = NULL;

static void IRAM_ATTR imu_isr_handler(void* arg) {
    s_int_count++;
}

static esp_err_t bmi270_read_regs(uint8_t reg_addr, uint8_t *data, size_t len) {
    if (!s_bmi270_dev_handle) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_bmi270_dev_handle, &reg_addr, 1, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t bmi270_write_reg(uint8_t reg_addr, uint8_t data) {
    if (!s_bmi270_dev_handle) return ESP_ERR_INVALID_STATE;
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(s_bmi270_dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
}

static esp_err_t bmi270_load_config(void) {
    esp_err_t err;
    uint16_t index = 0;
    uint8_t burst_buf[33]; // Reg + 32 bytes
    uint8_t chip_id = 0;

    // 0. Verify Chip ID (should be 0x24)
    err = bmi270_read_regs(BMI270_REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK || chip_id != 0x24) {
        ESP_LOGE(TAG, "Invalid Chip ID: 0x%02X (Expected 0x24)", chip_id);
        return ESP_ERR_INVALID_VERSION;
    }
    ESP_LOGI(TAG, "BMI270 (ID: 0x%02X) found. Loading %d bytes...", chip_id, (int)sizeof(bmi270_config_file));

    // 1. Disable power save
    bmi270_write_reg(BMI270_REG_PWR_CONF, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10)); // Bosch recommends a wait here

    // 2. Start Init sequence
    bmi270_write_reg(BMI270_REG_INIT_CTRL, 0x00);

    // 3. Burst Write in 32-byte chunks
    while (index < sizeof(bmi270_config_file)) {
        size_t chunk_size = sizeof(bmi270_config_file) - index;
        if (chunk_size > 32) chunk_size = 32;

        // CRITICAL FIX: The 12-bit word address is split 4-bit LSB / 8-bit MSB
        uint16_t word_addr = index / 2;
        uint8_t addr_ptr[3];
        addr_ptr[0] = BMI270_REG_INIT_ADDR_0; // 0x5B
        addr_ptr[1] = (uint8_t)(word_addr & 0x0F);       // Bottom 4 bits to Reg 0x5B
        addr_ptr[2] = (uint8_t)((word_addr >> 4) & 0xFF); // Remaining bits to Reg 0x5C
        
        err = i2c_master_transmit(s_bmi270_dev_handle, addr_ptr, 3, pdMS_TO_TICKS(100));
        if (err != ESP_OK) return err;

        burst_buf[0] = BMI270_REG_INIT_DATA; // 0x5E
        memcpy(&burst_buf[1], &bmi270_config_file[index], chunk_size);
        
        err = i2c_master_transmit(s_bmi270_dev_handle, burst_buf, chunk_size + 1, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2C error at index %d: %s", index, esp_err_to_name(err));
            return err;
        }

        index += chunk_size;
        if (index % 512 == 0) vTaskDelay(1); // Breather for bus
    }

    // 4. End Init
    bmi270_write_reg(BMI270_REG_INIT_CTRL, 0x01);
    vTaskDelay(pdMS_TO_TICKS(150)); // Mandatory wait for ASIC process

    uint8_t status = 0;
    bmi270_read_regs(BMI270_REG_INTERNAL_STATUS, &status, 1);
    if ((status & 0x0F) != 0x01) {
        ESP_LOGE(TAG, "Config load failed! Status: 0x%02X (Expected: x1)", status);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BMI270 Config loaded successfully.");
    return ESP_OK;
}




static void imu_task(void *pvParameters) {
    uint8_t raw_data[6];
    float ax, ay, az, magnitude;
    int64_t now;
    uint32_t log_counter = 0;

    while (1) {
        if (bmi270_read_regs(BMI270_REG_ACC_DATA, raw_data, 6) == ESP_OK) {
            int16_t ix = (int16_t)((raw_data[1] << 8) | raw_data[0]);
            int16_t iy = (int16_t)((raw_data[3] << 8) | raw_data[2]);
            int16_t iz = (int16_t)((raw_data[5] << 8) | raw_data[4]);

            ax = ix * 0.000061035f;
            ay = iy * 0.000061035f;
            az = iz * 0.000061035f;
            magnitude = sqrtf(ax*ax + ay*ay + az*az);

            if (log_counter < (30 * SAMPLE_RATE_HZ)) {
                if (log_counter % (SAMPLE_RATE_HZ / 2) == 0) {
                    ESP_LOGI(TAG, "G: [%.3f, %.3f, %.3f] RAW MAG: %.3f G | INTs: %lu", ax, ay, az, magnitude, (unsigned long)s_int_count);
                }
                log_counter++;
            }

            if (magnitude > s_shock_threshold) {
                now = esp_timer_get_time() / 1000;
                if (now - s_last_step_time > DEBOUNCE_MS) {
                    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
                    s_step_count++;
                    if (s_last_step_time > 0) {
                        float interval_min = (float)(now - s_last_step_time) / 60000.0f;
                        float instant_cadence = 1.0f / interval_min;
                        s_cadence = (s_cadence * 0.8f) + (instant_cadence * 0.2f);
                    }
                    s_last_step_time = now;
                    xSemaphoreGive(s_imu_mutex);
                    ESP_LOGI(TAG, "Step detected! Total: %lu, Cadence: %.1f", (unsigned long)s_step_count, s_cadence);
                }
            }
        }

        now = esp_timer_get_time() / 1000;
        if (now - s_last_step_time > 2000) {
            xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
            s_cadence = s_cadence * 0.9f;
            if (s_cadence < 10.0f) s_cadence = 0.0f;
            xSemaphoreGive(s_imu_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / SAMPLE_RATE_HZ));
    }
}

esp_err_t imu_service_init(void) {
    esp_err_t err;
    if (s_bmi270_dev_handle != NULL) return ESP_OK;

    s_i2c_bus_handle = bsp_i2c_get_handle();
    if (s_i2c_bus_handle == NULL) return ESP_ERR_INVALID_STATE;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMI270_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    err = i2c_master_bus_add_device(s_i2c_bus_handle, &dev_cfg, &s_bmi270_dev_handle);
    if (err != ESP_OK) return err;

    // 1. Soft Reset
    bmi270_write_reg(0x7E, 0xB6); 
    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. Load Config Blob (Firmware)
    if (bmi270_load_config() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load sensor configuration!");
        return ESP_FAIL;
    }

    // 3. Enable Accelerometer
    bmi270_write_reg(BMI270_REG_PWR_CTRL, 0x04); 
    
    // 4. Configure Accel: 200Hz ODR, Normal Power, Performance Mode enabled (0xA9)
    // Bit 7: perf_mode = 1 (High performance)
    // Bits 0-3: odr = 1001 (200Hz)
    bmi270_write_reg(BMI270_REG_ACC_CONF, 0xA9); 
    
    // 5. Force ±2G Range for maximum sensitivity
    bmi270_write_reg(BMI270_REG_ACC_RANGE, BMI270_RANGE_2G);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 6. Map DATA_READY to INT1
    bmi270_write_reg(0x53, 0x08); // INT1 Output Enable
    bmi270_write_reg(0x58, 0x04); // Map Data Ready

    // GPIO Config for Interrupt
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << IMU_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(IMU_INT_GPIO, imu_isr_handler, (void*) IMU_INT_GPIO);

    s_imu_mutex = xSemaphoreCreateMutex();
    xTaskCreate(imu_task, "imu_task", 4096, NULL, 10, NULL);

    return ESP_OK;
}

uint32_t imu_service_get_steps(void) {
    uint32_t steps;
    if (!s_imu_mutex) return 0;
    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    steps = s_step_count;
    xSemaphoreGive(s_imu_mutex);
    return steps;
}

float imu_service_get_cadence(void) {
    float cadence;
    if (!s_imu_mutex) return 0;
    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    cadence = s_cadence;
    xSemaphoreGive(s_imu_mutex);
    return cadence;
}

void imu_service_reset_steps(void) {
    if (!s_imu_mutex) return;
    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    s_step_count = 0;
    s_cadence = 0.0f;
    s_last_step_time = 0;
    xSemaphoreGive(s_imu_mutex);
}

void imu_service_set_sensitivity(uint8_t sensitivity_percent) {
    if (sensitivity_percent > 100) sensitivity_percent = 100;
    float new_threshold = 2.0f - ((float)sensitivity_percent / 100.0f) * 1.6f;
    if (s_imu_mutex) xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    s_shock_threshold = new_threshold;
    if (s_imu_mutex) xSemaphoreGive(s_imu_mutex);
}
