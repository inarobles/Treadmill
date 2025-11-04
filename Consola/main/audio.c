#include "audio.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>

static const char *TAG = "Audio";

// --- DEFINICIONES PARA EL PITIDO ---
#define BEEP_SAMPLE_RATE    (44100)
#define BEEP_CHANNELS       (1) // Mono
#define BEEP_BIT_DEPTH      (16)
#define BEEP_FREQUENCY      (1000.0f) // 1kHz
#define BEEP_DURATION_MS    (50)      // 50ms - más corto
#define BEEP_AMPLITUDE      (INT16_MAX / 8)

#define BEEP_BUFFER_SAMPLES (BEEP_SAMPLE_RATE * BEEP_DURATION_MS / 1000)
#define BEEP_BUFFER_SIZE    (BEEP_BUFFER_SAMPLES * BEEP_CHANNELS * (BEEP_BIT_DEPTH / 8))

static int16_t *g_beep_buffer = NULL;
static esp_codec_dev_handle_t g_speaker_handle = NULL;

// Queue para solicitudes de beep no-bloqueantes
static QueueHandle_t g_beep_queue = NULL;
#define BEEP_QUEUE_SIZE 10

static void generate_beep_buffer(void)
{
    g_beep_buffer = (int16_t *)malloc(BEEP_BUFFER_SIZE);
    if (!g_beep_buffer) {
        ESP_LOGE(TAG, "Fallo al alocar memoria para el buffer del pitido");
        return;
    }

    for (int i = 0; i < BEEP_BUFFER_SAMPLES; i++) {
        double angle = 2.0 * M_PI * BEEP_FREQUENCY * i / BEEP_SAMPLE_RATE;
        g_beep_buffer[i] = (int16_t)(BEEP_AMPLITUDE * sin(angle));
    }
    ESP_LOGI(TAG, "Buffer de pitido generado (%d bytes)", BEEP_BUFFER_SIZE);
}

// Task que reproduce los beeps de forma asíncrona
static void audio_beep_task(void *pvParameter)
{
    uint8_t beep_request;
    while (1) {
        // Esperar a que alguien solicite un beep
        if (xQueueReceive(g_beep_queue, &beep_request, portMAX_DELAY) == pdTRUE) {
            // Reproducir el beep (esto es bloqueante pero en otra task)
            if (g_speaker_handle && g_beep_buffer) {
                esp_codec_dev_write(g_speaker_handle, g_beep_buffer, BEEP_BUFFER_SIZE);
            }
        }
    }
}

// Función NO-BLOQUEANTE para solicitar un beep
void audio_play_beep(void)
{
    if (g_beep_queue) {
        uint8_t beep_req = 1;
        // No esperar si la queue está llena - simplemente ignorar
        xQueueSend(g_beep_queue, &beep_req, 0);
    }
}

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "Inicializando altavoz...");
    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BEEP_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    bsp_audio_init(&i2s_config);
    g_speaker_handle = bsp_audio_codec_speaker_init();
    if (g_speaker_handle == NULL) {
        ESP_LOGE(TAG, "Fallo al inicializar el altavoz");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BEEP_SAMPLE_RATE,
        .channel = BEEP_CHANNELS,
        .bits_per_sample = BEEP_BIT_DEPTH,
    };
    esp_codec_dev_open(g_speaker_handle, &fs);
    esp_codec_dev_set_out_vol(g_speaker_handle, 40.0);

    generate_beep_buffer();

    // Crear la queue y la task para beeps asíncronos
    g_beep_queue = xQueueCreate(BEEP_QUEUE_SIZE, sizeof(uint8_t));
    if (g_beep_queue == NULL) {
        ESP_LOGE(TAG, "Fallo al crear la queue de beeps");
        return ESP_FAIL;
    }

    if (xTaskCreate(audio_beep_task, "audio_beep", 2048, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Fallo al crear la task de audio");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Altavoz y buffer de pitido listos (modo asíncrono).");
    return ESP_OK;
}
