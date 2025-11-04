#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_touch_gsl3680.h"
#include "bsp/esp32_p4_function_ev_board.h"

#include "touch_driver.h"

static const char *TAG = "touch_driver";

#define BSP_I2C_SCL           (GPIO_NUM_8)
#define BSP_I2C_SDA           (GPIO_NUM_7)

#define LCD_TOUCH_RST     (GPIO_NUM_22)
#define LCD_TOUCH_INT     (GPIO_NUM_21)

#define LCD_H_RES         (800)
#define LCD_V_RES         (1280)

static i2c_master_bus_handle_t i2c_handle = NULL;
static esp_lcd_touch_handle_t touch_handle_p = NULL;

esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *touch_handle)
{
    // Usar el bus I2C compartido del BSP en lugar de crear uno nuevo
    i2c_handle = bsp_i2c_get_handle();
    if (!i2c_handle) {
        ESP_LOGE(TAG, "Failed to get BSP I2C handle");
        return ESP_FAIL;
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = LCD_TOUCH_RST,
        .int_gpio_num = LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 1,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    tp_io_config.scl_speed_hz = 100000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle), TAG, "New panel IO I2C failed");

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gsl3680(tp_io_handle, &tp_cfg, &touch_handle_p), TAG, "New touch failed");
    *touch_handle = touch_handle_p;

    return ESP_OK;
}

bool bsp_touch_read(esp_lcd_touch_handle_t touch_handle, uint16_t *x, uint16_t *y, uint8_t *touch_cnt)
{
    esp_lcd_touch_read_data(touch_handle);
    return esp_lcd_touch_get_coordinates(touch_handle, x, y, NULL, touch_cnt, 5);
}

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return touch_handle_p;
}