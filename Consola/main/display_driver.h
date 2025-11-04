#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_display_init(esp_lcd_panel_handle_t *panel_handle, SemaphoreHandle_t *refresh_finish_sem);

esp_lcd_panel_handle_t bsp_display_get_panel_handle(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_DRIVER_H