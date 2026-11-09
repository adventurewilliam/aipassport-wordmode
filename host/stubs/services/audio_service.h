/* 语法桩：真实见 components/core/services/include/services/audio_service.h */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "hal/hal_audio.h"
typedef struct { hal_audio_t *audio; uint8_t volume; } audio_service_t;
void audio_service_init(audio_service_t *, hal_audio_t *);
int  audio_service_configure(audio_service_t *, uint32_t hz, uint8_t bits, uint8_t ch);
void audio_service_set_volume(audio_service_t *, uint8_t percent);
int  audio_service_play(audio_service_t *, const void *pcm, size_t bytes);
int  audio_service_shutdown(audio_service_t *);
