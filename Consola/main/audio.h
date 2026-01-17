#ifndef AUDIO_H
#define AUDIO_H

#include "esp_err.h"
#ifndef SIMULATOR
#include "esp_codec_dev.h"
#endif

esp_err_t audio_init(void);
#ifndef SIMULATOR
esp_codec_dev_handle_t audio_get_mic_handle(void);
#endif
void audio_play_beep(void);
void audio_set_volume(uint8_t volume);
uint8_t audio_get_volume(void);

#endif // AUDIO_H
