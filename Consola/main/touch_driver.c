#include "touch_driver.h"
#include "esp_log.h"
#include "esp_lcd_touch.h"
#include "bsp/esp32_p4_function_ev_board.h"

static const char *TAG = "TOUCH_DEBUG";

void test_touch_read(void) {
    lv_indev_t *indev = bsp_display_get_input_dev();
    
    if (indev == NULL) {
        ESP_LOGE(TAG, "Input device is NULL!");
        return;
    }
    
    ESP_LOGI(TAG, "Input device found, testing reads...");
    
    // Try to read touch state manually
    lv_indev_data_t data;
    lv_indev_read(indev, &data);
    
    ESP_LOGI(TAG, "Touch state: %d, Point: (%d, %d)", 
             data.state, data.point.x, data.point.y);
}