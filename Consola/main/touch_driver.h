#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include "esp_err.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *touch_handle);

bool bsp_touch_read(esp_lcd_touch_handle_t touch_handle, uint16_t *x, uint16_t *y, uint8_t *touch_cnt);

esp_lcd_touch_handle_t bsp_touch_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_DRIVER_H