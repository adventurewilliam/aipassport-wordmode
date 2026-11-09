/* wordline.c —— 笔画指令解释器 wordline-v1（P0，纯逻辑层）
 *
 * 编译约束：只 include C99 标准头 + 本组件头，不依赖 LVGL / ESP-IDF / driver。
 *   gcc -std=c99 -Wall -Wextra -Werror -c wordline.c  必须零告警通过。
 *
 * 设计取舍：
 *   - 先整体 validate 再绘制，保证「坏数据一笔都不画」，不会画到一半出错；
 *   - 坐标映射用 int32 中间量 + 四舍五入，避免 8 位坐标在放大时溢出；
 *   - 零 malloc、零静态可变状态，天然可重入，真机任意时刻切词都安全。
 */
#include "services/wordline.h"
#include <stddef.h>

/* 0..200 归一化坐标 → box 内像素坐标（四舍五入） */
static int map_x(const wl_box_t *b, uint8_t v) {
    return b->x + (int)(((int32_t)v * b->w + WORDLINE_COORD_MAX / 2)
                        / WORDLINE_COORD_MAX);
}
static int map_y(const wl_box_t *b, uint8_t v) {
    return b->y + (int)(((int32_t)v * b->h + WORDLINE_COORD_MAX / 2)
                        / WORDLINE_COORD_MAX);
}
/* 半径按短边缩放，保证 box 非正方形时圆仍是圆 */
static int map_r(const wl_box_t *b, uint8_t v) {
    int side = (b->w < b->h) ? b->w : b->h;
    return (int)(((int32_t)v * side + WORDLINE_COORD_MAX / 2)
                 / WORDLINE_COORD_MAX);
}

int wordline_validate(const uint8_t *data, uint16_t len) {
    uint16_t need;
    int i, n;
    if (data == NULL || len < WORDLINE_HEADER_BYTES) {
        return WL_ERR_LENGTH;
    }
    if (data[0] != WORDLINE_MAGIC0 || data[1] != WORDLINE_MAGIC1) {
        return WL_ERR_MAGIC;
    }
    n = data[2];
    need = (uint16_t)(WORDLINE_HEADER_BYTES
                      + WORDLINE_STROKE_BYTES * n);
    if (len != need) {
        return WL_ERR_LENGTH;
    }
    for (i = 0; i < n; ++i) {
        uint8_t op = data[WORDLINE_HEADER_BYTES
                          + i * WORDLINE_STROKE_BYTES];
        if (op < WL_OP_LINE || op > WL_OP_DOT) {
            return WL_ERR_OPCODE;
        }
    }
    return n;
}

uint8_t wordline_stroke_count(const uint8_t *data, uint16_t len) {
    if (wordline_validate(data, len) < 0) {
        return 0;
    }
    return data[2];
}

int wordline_draw(const uint8_t *data, uint16_t len,
                  const wl_box_t *box, const wl_sink_t *sink) {
    int n, i;
    if (box == NULL || sink == NULL || box->w <= 0 || box->h <= 0) {
        return WL_ERR_ARG;
    }
    n = wordline_validate(data, len);
    if (n < 0) {
        return n;
    }
    for (i = 0; i < n; ++i) {
        const uint8_t *p = data + WORDLINE_HEADER_BYTES
                           + i * WORDLINE_STROKE_BYTES;
        uint8_t op = p[0], a = p[1], b = p[2], c = p[3], d = p[4];
        switch (op) {
        case WL_OP_LINE:
            if (sink->line) {
                sink->line(sink->user,
                           map_x(box, a), map_y(box, b),
                           map_x(box, c), map_y(box, d));
            }
            break;
        case WL_OP_CIRCLE:
            if (sink->circle) {
                sink->circle(sink->user, map_x(box, a), map_y(box, b),
                             map_r(box, c));
            }
            break;
        case WL_OP_RECT:
            if (sink->rect) {
                sink->rect(sink->user,
                           map_x(box, a), map_y(box, b),
                           map_x(box, c), map_y(box, d));
            }
            break;
        case WL_OP_DOT:
            if (sink->dot) {
                sink->dot(sink->user, map_x(box, a), map_y(box, b),
                          map_r(box, c));
            }
            break;
        default:
            /* validate 已保证不会走到这里 */
            break;
        }
    }
    return n;
}

const char *wordline_version(void) {
    return WORDLINE_VERSION;
}
