/* 语法桩：真实见 components/core/ports/include/hal/hal_kv.h */
#pragma once
#include <stdint.h>
typedef struct hal_kv_s { int unused; } hal_kv_t;
enum { HAL_KV_ERROR = -1, HAL_KV_NOT_FOUND = -2, HAL_KV_TOO_LARGE = -3 };
int hal_kv_get(hal_kv_t *c, const char *key, void *buf, int cap, int *out_len);
int hal_kv_set(hal_kv_t *c, const char *key, const void *data, int len);
