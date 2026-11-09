/* 语法桩：真实见 components/core/services/include/services/audio_clip.h */
#pragma once
#include <stdint.h>
#define AUDIO_CLIP_MAGIC 0x31504441u
typedef enum { CLIP_BOOT = 0, CLIP_CONNECT = 1, CLIP_GAMEOVER = 2, CLIP_COUNT } clip_id_t;
typedef struct {
    uint32_t magic;
    uint16_t sample_rate;
    uint8_t  channels;
    uint8_t  reserved;
    uint32_t num_samples;
} audio_clip_hdr_t;
