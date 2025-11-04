#include "speed_sensor.h"
#include "driver/pulse_cnt.h" // <-- API NUEVA Y CORRECTA
#include "driver/gpio.h"       // Para configurar pull-down
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h" // <-- CORRECCIÓN AÑADIDA

static const char *TAG_SPEED = "SPEED_SENSOR";

// Asignación de Pines v5
#define PCNT_GPIO       34 // Pin v5 para "Sensor Velocidad" (Solo Entrada)

static pcnt_unit_handle_t pcnt_unit = NULL;

void speed_sensor_init(void) {
    ESP_LOGI(TAG_SPEED, "Inicializando sensor de velocidad (PCNT) en GPIO %d", PCNT_GPIO);

    // Configurar GPIO con pull-down para evitar lecturas flotantes cuando el sensor no está conectado
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PCNT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_LOGI(TAG_SPEED, "GPIO %d configurado con pull-down para evitar ruido", PCNT_GPIO);

    pcnt_unit_config_t unit_config = {
        .low_limit = -1000,
        .high_limit = 1000,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000, // Ignora picos de ruido de menos de 1us
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = PCNT_GPIO,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan));

    // Configurar cómo contamos (Corregido para la nueva API)
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    
    ESP_LOGI(TAG_SPEED, "Sensor de velocidad (PCNT) inicializado y contando.");
}

int speed_sensor_get_pulse_count(void) {
    int pulse_count = 0;
    if (pcnt_unit) {
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit)); // Resetear
    }
    return pulse_count;
}