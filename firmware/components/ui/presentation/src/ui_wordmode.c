/* ui_wordmode.c —— 单词模式 7 屏 LVGL 表现层（P0）
 *
 * 内存铁律（ESP32-C3 无 PSRAM、LVGL 池固定 40 KiB）：
 *   - 全屏只用 1 个 lv_obj，所有内容在 LV_EVENT_DRAW_MAIN 里逐笔直绘；
 *   - 禁用 lv_canvas / lv_image / 全屏位图 / shadow（见契约测试）；
 *   - 线图来自 p0_pack.h 的 wordline-v1 笔画流，经 services/wordline.c 解析；
 *   - pcm_buf 为 1 KiB 静态全局，ADP1 分块解码，绝不整段展开。
 *
 * 绘制坐标：wordline 输出 0..200 归一化值 → wl_box_t 映射到屏幕像素；
 * draw 回调里再叠加根对象的绝对坐标偏移，兼容 LVGL 9 的 layer 坐标系。
 *
 * 本文件依赖 ESP-IDF 5.5.3 + LVGL 9.5，无法在 PC 沙箱编译；
 * 纯逻辑部分（word_mode.c / wordline.c）已在 host/ 下 gcc 真编译真测。
 * 真机接入前请按 integration/INTEGRATION.md 执行 idf.py build 复核。
 */
#include "presentation/ui_wordmode.h"
#include "services/wordline.h"
#include "services/adpcm.h"
#include "services/audio_clip.h"
#include <string.h>

/* 资产头位于 components/app/src/assets，presentation 的 CMakeLists 需把它
 * 加进 PRIV_INCLUDE_DIRS（见 firmware/patches/presentation-CMakeLists.patch）。 */
#include "p0_pack.h"
#include "p0_audio.h"

/* ---------------- 配色与布局（PRD §4 线稿） ---------------- */
#define WM_BG       0xfbfaf6   /* 纸白 */
#define WM_INK      0x202a33   /* 深墨 */
#define WM_ACCENT   0xe07a3f   /* 暖杏焦点 */
#define WM_MUTED    0x9aa7b2   /* 进度点未到 */

#define SCR_W       240
#define SCR_H       320
#define BOX_ART     { 40, 62, 160, 160 }   /* ③ 中部 160×160 线图 */
#define BOX_BTN_L   { 24, 246, 84, 52 }    /* ③ 左：拇指「认识」 */
#define BOX_BTN_R   { 132, 246, 84, 52 }   /* ③ 右：问号「不记得」 */
#define BOX_ICON    { 76, 96, 88, 88 }     /* ④⑤⑥ 居中图案 */
#define ICON_STROKE 3

/* ---------------- 图标：同样用 wordline-v1 小笔画流，复用同一绘制器 ----------------
 * 归一化 0..200；opcode 1=LINE 2=CIRCLE 3=RECT 4=DOT。 */
#define WL(n) 0x57, 0x4C, (n)
static const uint8_t ICON_THUMB[] = {   /* 大拇哥：手腕 + 竖起的拇指 */
    WL(6),
    1, 55, 175, 55, 100,   1, 55, 100, 78, 88,
    1, 78, 88, 92, 88,     1, 92, 88, 100, 100,
    1, 100, 100, 150, 120, 1, 150, 120, 150, 175,
};
static const uint8_t ICON_QUESTION[] = { /* 问号：弧 + 点 */
    WL(7),
    1, 55, 70, 62, 48,     1, 62, 48, 85, 40,
    1, 85, 40, 120, 48,    1, 120, 48, 140, 72,
    1, 140, 72, 118, 98,   1, 118, 98, 100, 118,
    4, 100, 155, 8, 0,
};
static const uint8_t ICON_MIC[] = {      /* 麦克风 + 底座 */
    WL(7),
    3, 78, 35, 122, 108,   1, 60, 88, 60, 108,
    1, 60, 108, 75, 128,   1, 75, 128, 125, 128,
    1, 125, 128, 140, 108, 1, 140, 108, 140, 88,
    1, 100, 128, 100, 165,
};
static const uint8_t ICON_STAR[] = {     /* 五角星 */
    WL(10),
    1, 100, 20, 123, 78,   1, 123, 78, 178, 78,
    1, 178, 78, 137, 114,  1, 137, 114, 152, 170,
    1, 152, 170, 100, 136, 1, 100, 136, 48, 170,
    1, 48, 170, 63, 114,   1, 63, 114, 22, 78,
    1, 22, 78, 77, 78,     1, 77, 78, 100, 20,
};
static const uint8_t ICON_EAR[] = {      /* 耳朵 + 循环箭头 */
    WL(9),
    1, 120, 45, 85, 45,    1, 85, 45, 55, 75,
    1, 55, 75, 55, 125,    1, 55, 125, 82, 158,
    1, 82, 158, 115, 150,  1, 90, 78, 110, 92,
    1, 110, 92, 92, 118,   1, 135, 60, 165, 78,
    1, 165, 78, 172, 112,
};
static const uint8_t ICON_ARROW[] = {    /* 循环箭头头部 */
    WL(2),
    1, 152, 100, 172, 112, 1, 168, 92, 172, 112,
};

/* 16 点单位圆（cos/sin × 256），零 FPU、零 math.h */
static const int16_t CIRCLE_X[16] = {
    256, 237, 181, 98, 0, -98, -181, -237,
    -256, -237, -181, -98, 0, 98, 181, 237
};
static const int16_t CIRCLE_Y[16] = {
    0, 98, 181, 237, 256, 237, 181, 98,
    0, -98, -181, -237, -256, -237, -181, -98
};

/* ---------------- 绘制上下文 ---------------- */
typedef struct {
    lv_layer_t *layer;
    lv_color_t  pen;
    int32_t     ox, oy;      /* 根对象绝对坐标偏移 */
} pen_t;

typedef struct {
    wm_screen_t screen;
    int         word_index;
} draw_ctx_t;

static lv_obj_t          *s_screen;
static lv_obj_t          *s_prev;
static draw_ctx_t         s_ctx;
static ui_wordmode_menu_cb_t s_on_menu;
static void              *s_menu_user;
static bool               s_suppress_click;  /* 长按后松手不二次触发 */

/* ---------------- 发音：ADP1 分块非阻塞解码 ---------------- */
#define WM_PCM_SAMPLES   512            /* 1 KiB 静态缓冲 */
#define WM_ADPCM_CHUNK   256            /* 每块 256 B → 512 样本 */
static int16_t        s_pcm[WM_PCM_SAMPLES];
static adpcm_state_t  s_adpcm;
static audio_service_t *s_audio;
static const uint8_t *s_clip;
static uint32_t       s_clip_len;
static uint32_t       s_clip_off;
static uint32_t       s_samples_out;
static bool           s_playing;

/* ---------------- LVGL 9 直线绘制 ---------------- */
static void pen_line(void *user, int x1, int y1, int x2, int y2) {
    pen_t *p = (pen_t *)user;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = p->pen;
    d.width = ICON_STROKE;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = p->ox + x1; d.p1.y = p->oy + y1;
    d.p2.x = p->ox + x2; d.p2.y = p->oy + y2;
    lv_draw_line(p->layer, &d);
}
static void pen_circle(void *user, int cx, int cy, int r) {
    int i;
    for (i = 0; i < 16; ++i) {
        int j = (i + 1) & 15;
        int x1 = cx + (int)((int32_t)CIRCLE_X[i] * r / 256);
        int y1 = cy + (int)((int32_t)CIRCLE_Y[i] * r / 256);
        int x2 = cx + (int)((int32_t)CIRCLE_X[j] * r / 256);
        int y2 = cy + (int)((int32_t)CIRCLE_Y[j] * r / 256);
        pen_line(user, x1, y1, x2, y2);
    }
}
static void pen_rect(void *user, int x1, int y1, int x2, int y2) {
    pen_line(user, x1, y1, x2, y1);
    pen_line(user, x2, y1, x2, y2);
    pen_line(user, x2, y2, x1, y2);
    pen_line(user, x1, y2, x1, y1);
}
static void pen_dot(void *user, int cx, int cy, int r) {
    pen_circle(user, cx, cy, r);
}
static wl_sink_t pen_sink(pen_t *p) {
    wl_sink_t s;
    s.line = pen_line; s.circle = pen_circle;
    s.rect = pen_rect; s.dot = pen_dot;
    s.user = p;
    return s;
}

/* 画一个带边框的按钮（4 条线，零对象） */
static void draw_button_frame(pen_t *p, const wl_box_t *b, bool focus) {
    p->pen = lv_color_hex(focus ? WM_ACCENT : WM_INK);
    pen_rect(p, b->x, b->y, b->x + b->w, b->y + b->h);
}

/* 画一个 wordline 图标流 */
static void draw_icon(pen_t *p, const uint8_t *data, uint16_t len,
                      const wl_box_t *box, uint32_t color) {
    wl_sink_t s = pen_sink(p);
    p->pen = lv_color_hex(color);
    (void)wordline_draw(data, len, box, &s);
}

/* ---------------- 7 屏绘制总入口 ---------------- */
static void draw_cb(lv_event_t *e) {
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_area_t a;
    pen_t p;
    wl_sink_t s;
    int i;
    const wl_box_t box_art = BOX_ART;
    const wl_box_t box_l   = BOX_BTN_L;
    const wl_box_t box_r   = BOX_BTN_R;
    const wl_box_t box_ico = BOX_ICON;

    lv_obj_get_coords(obj, &a);
    p.layer = lv_event_get_layer(e);
    p.ox = a.x1; p.oy = a.y1;
    p.pen = lv_color_hex(WM_INK);

    /* 顶部细状态条 */
    pen_line(&p, 20, 48, 220, 48);

    /* 进度点：10 个，当前词暖杏 */
    for (i = 0; i < WORD_P0_COUNT; ++i) {
        int cx = 28 + i * 20, cy = 30;
        int r = (i == s_ctx.word_index) ? 5 : 3;
        p.pen = lv_color_hex(i <= s_ctx.word_index ? WM_ACCENT : WM_MUTED);
        pen_circle(&p, cx, cy, r);
    }

    p.pen = lv_color_hex(WM_INK);
    switch (s_ctx.screen) {
    case WM_SCREEN_SHOW:
        /* ③ 中部线图 */
        s = pen_sink(&p);
        (void)wordline_draw(kWordP0[s_ctx.word_index].strokes,
                            kWordP0[s_ctx.word_index].len, &box_art, &s);
        /* ③ 两个 84×52 图案按钮：左拇指、右问号 */
        draw_button_frame(&p, &box_l, false);
        draw_button_frame(&p, &box_r, false);
        draw_icon(&p, ICON_THUMB, sizeof(ICON_THUMB), &box_l, WM_INK);
        draw_icon(&p, ICON_QUESTION, sizeof(ICON_QUESTION), &box_r, WM_INK);
        break;

    case WM_SCREEN_LISTEN:
        /* ④ 跟读占位：麦克风，P0 不采音 */
        draw_icon(&p, ICON_MIC, sizeof(ICON_MIC), &box_ico, WM_ACCENT);
        break;

    case WM_SCREEN_KNOWN:
        /* ⑤ 认识 / 读对了：大拇哥 + 星星，暖杏高亮，刻意无失败符号 */
        draw_icon(&p, ICON_THUMB, sizeof(ICON_THUMB), &box_ico, WM_ACCENT);
        draw_icon(&p, ICON_STAR, sizeof(ICON_STAR), &box_ico, WM_INK);
        break;

    case WM_SCREEN_UNKNOWN:
        /* ⑥ 不记得：耳朵 + 循环箭头，播慢速；仍无任何失败符号 */
        draw_icon(&p, ICON_EAR, sizeof(ICON_EAR), &box_ico, WM_INK);
        draw_icon(&p, ICON_ARROW, sizeof(ICON_ARROW), &box_ico, WM_ACCENT);
        break;

    case WM_SCREEN_MENU:
    default:
        /* ②′ 菜单由既有 ui_plays 覆盖实现，本层不绘制 */
        break;
    }
}

/* ---------------- 公共接口 ---------------- */
void ui_wordmode_set_audio(audio_service_t *svc) {
    s_audio = svc;
}

void ui_wordmode_open(ui_wordmode_menu_cb_t on_menu, void *user) {
    if (s_screen) return;
    s_on_menu = on_menu;
    s_menu_user = user;
    s_suppress_click = false;
    s_prev = lv_screen_active();
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, SCR_W, SCR_H);
    lv_obj_set_pos(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(WM_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    s_ctx.screen = WM_SCREEN_SHOW;
    s_ctx.word_index = 0;
    lv_obj_add_event_cb(s_screen, draw_cb, LV_EVENT_DRAW_MAIN, &s_ctx);
    lv_screen_load(s_screen);
}

void ui_wordmode_render(wm_screen_t screen, int word_index) {
    if (!s_screen) return;
    if (word_index < 0 || word_index >= WORD_P0_COUNT) word_index = 0;
    s_ctx.screen = screen;
    s_ctx.word_index = word_index;
    lv_obj_invalidate(s_screen);   /* 同屏内改词只重绘，不重建对象 */
}

void ui_wordmode_key(ui_play_key_t key, hal_btn_event_t event) {
    int idx;
    if (key == UI_PLAY_UP)        idx = WM_KEY_UP;
    else if (key == UI_PLAY_DOWN) idx = WM_KEY_DOWN;
    else if (key == UI_PLAY_OK)   idx = WM_KEY_OK;
    else return;                    /* UI_PLAY_BACK 不参与单词模式 */

    switch (event) {
    case HAL_BTN_PRESS:
        /* PRESS 求跟手：UP/DOWN 图案反馈立即出，OK 留给 CLICK 去双击窗口 */
        if (idx == WM_KEY_UP || idx == WM_KEY_DOWN) {
            word_mode_key(idx, false);
        }
        break;
    case HAL_BTN_LONG:
        if (idx == WM_KEY_OK) {
            s_suppress_click = true;
            if (s_on_menu) s_on_menu(s_menu_user);
            word_mode_key(WM_KEY_OK, true);   /* → WM_ACT_MENU_REQUEST */
        }
        break;
    case HAL_BTN_CLICK:
        if (idx == WM_KEY_OK) {
            if (s_suppress_click) {           /* 长按后的松手，忽略 */
                s_suppress_click = false;
                break;
            }
            word_mode_key(WM_KEY_OK, false);
        }
        break;
    case HAL_BTN_RELEASE:
    case HAL_BTN_DOUBLE:
    default:
        break;
    }
}

void ui_wordmode_stop_audio(void) {
    s_playing = false;
    s_clip = NULL;
    s_clip_off = 0;
    s_clip_len = 0;
}

/* 按小端读 4 字节，避免对齐访问 */
static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
           | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* 找词对应的 ADP1 段；找不到返回 NULL */
static word_clip_t find_clip(const char *word, bool slow, int *out_index) {
    int i;
    word_clip_t empty = { NULL, 0 };
    for (i = 0; i < WORD_AUDIO_COUNT; ++i) {
        if (strcmp(kWordAudio[i].word, word) == 0) {
            if (out_index) *out_index = i;
            return slow ? kWordAudio[i].slow : kWordAudio[i].normal;
        }
    }
    if (out_index) *out_index = -1;
    return empty;
}

int ui_wordmode_play(const char *word, bool slow) {
    word_clip_t clip;
    uint32_t magic, num_samples;
    uint16_t rate;
    uint8_t channels;
    int idx = 0;

    if (!s_audio) return -1;
    if (!word && s_ctx.word_index >= 0 && s_ctx.word_index < WORD_P0_COUNT) {
        word = kWordP0[s_ctx.word_index].word;
    }
    if (!word) return -1;

    clip = find_clip(word, slow, &idx);
    if (!clip.data || clip.len < 12) return -2;
    magic = rd_u32le(clip.data);
    rate = rd_u16le(clip.data + 4);
    channels = clip.data[6];
    num_samples = rd_u32le(clip.data + 8);
    if (magic != WORD_AUDIO_MAGIC || rate != WORD_AUDIO_RATE ||
        channels != 1 || num_samples == 0) {
        return -3;   /* ADP1 头校验失败，静默跳过，不给孩子报错符号 */
    }

    (void)audio_service_configure(s_audio, WORD_AUDIO_RATE, 16, 1);
    adpcm_state_reset(&s_adpcm);
    s_clip = clip.data;
    s_clip_len = clip.len;
    s_clip_off = 12;                 /* 跳过 12 字节 ADP1 头 */
    s_samples_out = 0;
    s_playing = true;
    return (int)ui_wordmode_audio_tick();   /* 立刻推出第一块，不阻塞 LVGL */
}

/* 每次 LVGL 定时器（50 ms）推一块；返回累计解码样本数。
 * 由 app 层 wordmode_app_pump() 调用，避免 audio_service_play 阻塞 LVGL 任务。 */
uint32_t ui_wordmode_audio_tick(void) {
    int n, samples;
    if (!s_playing || !s_clip || !s_audio) return s_samples_out;
    if (s_clip_off >= s_clip_len) {
        s_playing = false;
        return s_samples_out;
    }
    n = (int)(s_clip_len - s_clip_off);
    if (n > WM_ADPCM_CHUNK) n = WM_ADPCM_CHUNK;
    samples = adpcm_decode(&s_adpcm, s_clip + s_clip_off, n, s_pcm);
    if (samples > 0) {
        (void)audio_service_play(s_audio, s_pcm,
                                 (size_t)samples * sizeof(int16_t));
        s_samples_out += (uint32_t)samples;
    }
    s_clip_off += (uint32_t)n;
    if (s_clip_off >= s_clip_len) s_playing = false;
    return s_samples_out;
}

bool ui_wordmode_audio_active(void) {
    return s_playing;
}

void ui_wordmode_close(void) {
    if (!s_screen) return;
    ui_wordmode_stop_audio();
    {
        lv_obj_t *old = s_screen;
        if (s_prev) lv_screen_load(s_prev);
        s_screen = NULL; s_prev = NULL;
        lv_obj_delete_async(old);
    }
}

bool ui_wordmode_active(void) {
    return s_screen != NULL;
}
