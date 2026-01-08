#ifndef AUDIO_H
#define AUDIO_H

#include "esp_err.h"

esp_err_t audio_init(void);
void audio_play_beep(void);
void audio_set_volume(uint8_t volume);
uint8_t audio_get_volume(void);

#endif // AUDIO_H
