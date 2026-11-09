/* 语法桩：真实见 components/core/services/include/services/adpcm.h */
#pragma once
#include <stdint.h>
typedef struct { int16_t predictor; int16_t index; } adpcm_state_t;
void adpcm_state_reset(adpcm_state_t *st);
int  adpcm_decode(adpcm_state_t *st, const uint8_t *in, int nbytes, int16_t *out);
