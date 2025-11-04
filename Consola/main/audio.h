#ifndef AUDIO_H
#define AUDIO_H

#include "esp_err.h"

esp_err_t audio_init(void);
void audio_play_beep(void);

#endif // AUDIO_H
