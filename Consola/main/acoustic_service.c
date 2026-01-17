#include "acoustic_service.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "esp_codec_dev.h"
#include "audio.h"
#include "treadmill_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"

static const char *TAG = "ACOUSTIC_SVC";

// --- Configuración de Audio ---
#define MIC_SAMPLE_RATE     44100
#define MIC_CHANNELS        1
#define MIC_BIT_DEPTH       16
#define SAMPLES_PER_FRAME   441   // 10ms a 44.1kHz
#define READ_LEN_BYTES      (SAMPLES_PER_FRAME * sizeof(int16_t))

// --- Parámetros de Detección Quirúrgica (Optimizado 10 km/h) ---
#define ALPHA_ENVELOPE      0.12f // Más suave para capturar la "masa" del paso
#define ALPHA_BG            0.995f // Muy estable para el ruido del motor
#define MIN_STEP_INTERVAL   320   // ms (Cerca de 185 SPM máximo)

// --- Estado Global ---
static float s_threshold_profile[MAX_PROFILE_SPEED];
static float s_current_speed_kmh = 0.0f;
static uint32_t s_step_count = 0;
static float s_cadence = 0.0f;
static int64_t s_last_step_time = 0;
static SemaphoreHandle_t s_mic_mutex = NULL;
static int64_t s_touch_silence_until_ms = 0;

// --- Debug Dump ---
#define DUMP_SECONDS       2
#define DUMP_BUFFER_SIZE   (MIC_SAMPLE_RATE * DUMP_SECONDS)
static int16_t *s_dump_buffer = NULL;
static uint32_t s_dump_index = 0;
static bool s_dump_requested = false;

// --- Estado de Calibración ---
typedef enum { CAL_IDLE, CAL_MEASURING, CAL_DONE } cal_state_t;
static cal_state_t s_cal_state = CAL_IDLE;
static float s_cal_max_energy = 0.0f;
static uint32_t s_cal_frames = 0;
#define CAL_TOTAL_FRAMES    300 // 3 segundos de calibración

static esp_codec_dev_handle_t s_mic_handle = NULL;

// --- Persistencia NVS ---
static esp_err_t save_profile_to_nvs(void) {
    nvs_handle_t nvs_h;
    esp_err_t err = nvs_open("acoustic_svc", NVS_READWRITE, &nvs_h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs_h, "thr_profile", s_threshold_profile, sizeof(s_threshold_profile));
    if (err == ESP_OK) err = nvs_commit(nvs_h);
    nvs_close(nvs_h);
    return err;
}

static void load_profile_from_nvs(void) {
    for (int i = 0; i < MAX_PROFILE_SPEED; i++) s_threshold_profile[i] = 0.0f;
    nvs_handle_t nvs_h;
    if (nvs_open("acoustic_svc", NVS_READONLY, &nvs_h) == ESP_OK) {
        size_t size = sizeof(s_threshold_profile);
        nvs_get_blob(nvs_h, "thr_profile", s_threshold_profile, &size);
        nvs_close(nvs_h);
    }
}

// --- Filtro Digital IIR (Banda: 60Hz - 600Hz @ 44.1kHz) ---
// Coeficientes para un paso-banda simple (basado en el perfil observado)
typedef struct {
    float x1, x2, y1, y2;
} iir_filter_t;
static iir_filter_t s_filter = {0};

static float process_sample_filter(float x) {
    // Filtro simplificado de 2 polos para centrar energía en la pisada
    // Esto elimina vibraciones DC y ruidos de alta frecuencia del motor
    float y = 0.046f * (x - s_filter.x2) + 1.89f * s_filter.y1 - 0.90f * s_filter.y2;
    s_filter.x2 = s_filter.x1; s_filter.x1 = x;
    s_filter.y2 = s_filter.y1; s_filter.y1 = y;
    return y;
}

static void acoustic_task(void *pvParameters) {
    int16_t samples[SAMPLES_PER_FRAME];
    float envelope = 0.0f;
    float background = 0.0f;
    uint32_t log_counter = 0;
    
    esp_codec_dev_set_in_gain(s_mic_handle, 42.0f); // GANANCIA MÁXIMA (42dB)

    ESP_LOGI(TAG, "Tarea Acústica: MODO QUIRÚRGICO (Optimizado 10 km/h)");

    while (1) {
        esp_err_t err = esp_codec_dev_read(s_mic_handle, samples, READ_LEN_BYTES);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // --- Captura de Dump (Mismo sistema) ---
        if (s_dump_requested && s_dump_buffer) {
            uint32_t rem = DUMP_BUFFER_SIZE - s_dump_index;
            uint32_t to_copy = (rem > SAMPLES_PER_FRAME) ? SAMPLES_PER_FRAME : rem;
            memcpy(&s_dump_buffer[s_dump_index], samples, to_copy * sizeof(int16_t));
            s_dump_index += to_copy;
            if (s_dump_index >= DUMP_BUFFER_SIZE) {
                s_dump_requested = false;
                printf("---AUDIO_DUMP_START---\n");
                for (int i = 0; i < DUMP_BUFFER_SIZE; i++) {
                    printf("%04hX", (unsigned short)s_dump_buffer[i]);
                    if (i % 32 == 31) printf("\n");
                }
                printf("\n---AUDIO_DUMP_END---\n");
                s_dump_index = 0;
            }
        }

        // 1. Filtrado y Cálculo de Energía (MAV con Boost Digital x4)
        float frame_energy = 0.0f;
        for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
            float x = (float)samples[i] / 32768.0f;
            float filtered_x = process_sample_filter(x);
            frame_energy += fabsf(filtered_x);
        }
        frame_energy = (frame_energy / SAMPLES_PER_FRAME) * 4.0f; // Boost x4

        // 2. Seguimiento de Envolvente y Fondo del Filtro
        envelope = (envelope * (1.0f - ALPHA_ENVELOPE)) + (frame_energy * ALPHA_ENVELOPE);
        background = (background * ALPHA_BG) + (frame_energy * (1.0f - ALPHA_BG));

        // 3. Cálculo de Desviación de Ataque
        float attack = envelope - background;
        if (attack < 0) attack = 0;

        int64_t now_ms = esp_timer_get_time() / 1000;
        
        // 4. Lógica de Calibración
        if (s_cal_state == CAL_MEASURING) {
            if (attack > s_cal_max_energy) s_cal_max_energy = attack;
            s_cal_frames++;
            if (s_cal_frames >= CAL_TOTAL_FRAMES) {
                s_cal_state = CAL_DONE;
                ESP_LOGI(TAG, "Mapeo Motor: Speed %.1f km/h -> Ruido Atk: %.5f", s_current_speed_kmh, s_cal_max_energy);
            }
        } else if (s_current_speed_kmh >= 4.0f && s_current_speed_kmh <= 19.5f && now_ms > s_touch_silence_until_ms) {
            
            // 5. Umbral Dinámico Adaptativo (Motor Noise Floor + Safety Margin)
            int spd_idx = (int)s_current_speed_kmh;
            if (spd_idx >= MAX_PROFILE_SPEED) spd_idx = MAX_PROFILE_SPEED - 1;

            float noise_floor = s_threshold_profile[spd_idx];
            
            // Fallback si no hay calibración (Valor por defecto sensible)
            if (noise_floor < 0.001f) noise_floor = 0.010f; 

            // El umbral se calcula escalando el ruido del motor.
            // 100% (Sensible) -> Factor 1.2 (Dispara con poco ataque extra)
            // 0% (Sordo) -> Factor 4.0 (Exige un ataque mucho más fuerte que el ruido del motor)
            float sense_val = (float)g_treadmill_state.pedometer_sensitivity;
            float factor = 1.2f + (100.0f - sense_val) * 0.028f; 
            float current_trigger = noise_floor * factor;

            // Log de diagnóstico cada 500ms
            if (log_counter % 50 == 0) {
                 ESP_LOGI(TAG, "POD: Speed=%.1f, Atk=%.4f, Thr=%.4f (Sens: %d%%)", 
                          s_current_speed_kmh, attack, current_trigger, (int)sense_val);
            }

            // 6. Detección por "Hit" Rítmico
            static bool armed = true;
            if (armed && attack > current_trigger) {
                int64_t interval = now_ms - s_last_step_time;
                
                // Ventana Rítmica: Ignorar falsos positivos mecánicos inmediatos
                if (interval > MIN_STEP_INTERVAL) {
                    xSemaphoreTake(s_mic_mutex, portMAX_DELAY);
                    s_step_count++;
                    
                    if (s_last_step_time > 0) {
                        float instant_cadence = 60000.0f / (float)interval;
                        if (instant_cadence < 210.0f) {
                            s_cadence = (s_cadence * 0.8f) + (instant_cadence * 0.2f);
                        }
                    } else {
                        s_cadence = 165.0f; // Inicial para corredor a 10 km/h
                    }
                    
                    s_last_step_time = now_ms;
                    xSemaphoreGive(s_mic_mutex);
                    armed = false;

                    ESP_LOGI(TAG, "¡PASO! [Energía: %.4f, Filt: %.4f, SPM: %.1f]", attack, frame_energy, s_cadence);
                }
            }

            // Rearme (Histéresis)
            if (!armed && attack < (current_trigger * 0.7f)) {
                armed = true;
            }
        }

        if (log_counter % 200 == 0 && s_current_speed_kmh > 0.1f) {
            ESP_LOGD(TAG, "Atk: %.5f, BG: %.5f, Thr: %.5f", attack, background, (s_threshold_profile[(int)s_current_speed_kmh] ?: 0.012f));
        }
        log_counter++;

        if (now_ms - s_last_step_time > 2000) {
            xSemaphoreTake(s_mic_mutex, portMAX_DELAY);
            s_cadence *= 0.98f;
            if (s_cadence < 5.0f) s_cadence = 0.0f;
            xSemaphoreGive(s_mic_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t acoustic_service_init(void) {
    if (s_mic_handle != NULL) return ESP_OK;

    s_mic_handle = audio_get_mic_handle();
    if (!s_mic_handle) return ESP_FAIL;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = MIC_SAMPLE_RATE,
        .channel = MIC_CHANNELS,
        .bits_per_sample = MIC_BIT_DEPTH,
    };
    
    if (esp_codec_dev_open(s_mic_handle, &fs) != ESP_OK) return ESP_FAIL;

    load_profile_from_nvs();
    s_mic_mutex = xSemaphoreCreateMutex();
    
    // Alocar buffer de dump en PSRAM si es posible
    s_dump_buffer = (int16_t *)heap_caps_malloc(DUMP_BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_dump_buffer) s_dump_buffer = (int16_t *)malloc(DUMP_BUFFER_SIZE * sizeof(int16_t));

    xTaskCreate(acoustic_task, "acoustic_task", 4096, NULL, 10, NULL);
    return ESP_OK;
}

uint32_t acoustic_service_get_steps(void) {
    if (!s_mic_mutex) return 0;
    xSemaphoreTake(s_mic_mutex, portMAX_DELAY);
    uint32_t val = s_step_count;
    xSemaphoreGive(s_mic_mutex);
    return val;
}

float acoustic_service_get_cadence(void) {
    if (!s_mic_mutex) return 0;
    xSemaphoreTake(s_mic_mutex, portMAX_DELAY);
    float val = s_cadence;
    xSemaphoreGive(s_mic_mutex);
    return val;
}

void acoustic_service_reset_steps(void) {
    if (!s_mic_mutex) return;
    xSemaphoreTake(s_mic_mutex, portMAX_DELAY);
    s_step_count = 0; s_cadence = 0.0f; s_last_step_time = 0;
    xSemaphoreGive(s_mic_mutex);
}

void acoustic_service_set_current_speed(float speed_kmh) {
    s_current_speed_kmh = speed_kmh;
}

void acoustic_service_start_auto_calibration(void) {
    s_cal_max_energy = 0.0f; s_cal_frames = 0; s_cal_state = CAL_MEASURING;
}

float acoustic_service_get_measured_noise(void) { return s_cal_max_energy; }

void acoustic_service_set_speed_threshold(uint8_t kmh, float threshold) {
    if (kmh < MAX_PROFILE_SPEED) {
        s_threshold_profile[kmh] = threshold;
        save_profile_to_nvs();
        ESP_LOGI(TAG, "NVS: Guardado ruido base para %d km/h = %.5f", kmh, threshold);
    }
}

bool acoustic_service_is_calibrating(void) { return (s_cal_state == CAL_MEASURING); }

void acoustic_service_silence_for_touch(void) {
    s_touch_silence_until_ms = (esp_timer_get_time() / 1000) + 400;
}

void acoustic_service_dump_samples(void) {
    s_dump_index = 0;
    s_dump_requested = true;
    ESP_LOGI(TAG, "Iniciando captura de audio de 2s para diagnóstico...");
}

