#ifndef AUDIO_H
#define AUDIO_H
#include <stdbool.h>
#include "save.h"
void audio_init(void);
void audio_frame(void);
void audio_settings(const SaveData *settings);
void audio_music(int clip);
void audio_voice(int clip);
void audio_stop_voice(void);
bool audio_voice_active(void);
#endif
