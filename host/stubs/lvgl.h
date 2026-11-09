/* host/stubs/lvgl.h —— 仅用于 gcc -fsyntax-only 的最小 LVGL 9 类型桩。
 * 不是 LVGL 实现，只为在 PC 上验证 ui_wordmode.c 的语法与接口形状。
 * 真机请用 ESP-IDF 的 lvgl 组件头文件。 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct { uint8_t blue, green, red; } lv_color_t;
typedef int32_t lv_coord_t;
typedef struct { lv_coord_t x1, y1, x2, y2; } lv_area_t;
typedef struct { int32_t x, y; } lv_point_precise_t;
typedef struct lv_layer_s { int unused; } lv_layer_t;
typedef struct lv_obj_s { int unused; } lv_obj_t;
typedef struct lv_event_s { int unused; } lv_event_t;

typedef struct {
    lv_color_t color;
    int32_t width;
    uint8_t round_start, round_end, opa;
    lv_point_precise_t p1, p2;
} lv_draw_line_dsc_t;

lv_color_t lv_color_hex(uint32_t hex);
void lv_draw_line_dsc_init(lv_draw_line_dsc_t *d);
void lv_draw_line(lv_layer_t *layer, const lv_draw_line_dsc_t *d);

lv_layer_t *lv_event_get_layer(lv_event_t *e);
lv_obj_t   *lv_event_get_target(lv_event_t *e);
void        lv_obj_get_coords(lv_obj_t *obj, lv_area_t *out);
void        lv_obj_add_event_cb(lv_obj_t *obj, void (*cb)(lv_event_t *),
                                int filter, void *user);
lv_obj_t   *lv_obj_create(lv_obj_t *parent);
void        lv_obj_remove_style_all(lv_obj_t *obj);
void        lv_obj_set_size(lv_obj_t *obj, int32_t w, int32_t h);
void        lv_obj_set_pos(lv_obj_t *obj, int32_t x, int32_t y);
void        lv_obj_set_style_bg_color(lv_obj_t *obj, lv_color_t c, int sel);
void        lv_obj_set_style_bg_opa(lv_obj_t *obj, uint8_t opa, int sel);
void        lv_obj_remove_flag(lv_obj_t *obj, int flag);
lv_obj_t   *lv_screen_active(void);
void        lv_screen_load(lv_obj_t *scr);
void        lv_obj_invalidate(lv_obj_t *obj);
void        lv_obj_delete_async(lv_obj_t *obj);
bool        lv_obj_is_valid(const lv_obj_t *obj);

#define LV_OPA_COVER            255
#define LV_EVENT_DRAW_MAIN      0x10
#define LV_OBJ_FLAG_SCROLLABLE  0x0001
