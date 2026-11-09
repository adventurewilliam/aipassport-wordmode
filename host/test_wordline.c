/* test_wordline.c —— 笔画指令解释器 PC 单测（无需 ESP-IDF / LVGL）
 *
 * 编译运行（由 host/Makefile 自动完成）：
 *   gcc -std=c99 -Wall -Wextra -Werror \
 *       -I../firmware/components/core/services/include \
 *       -I../firmware/components/app/src/assets \
 *       test_wordline.c ../firmware/components/core/services/src/wordline.c \
 *       -o test_wordline && ./test_wordline
 *
 * 直接吃真实资产 p0_pack.h，验证 10 词 / 149 笔全部可解析、坐标映射正确、
 * 以及坏数据一笔不画。
 */
#include <stdio.h>
#include <string.h>
#include "services/wordline.h"
#include "p0_pack.h"     /* 真实生成的 10 词笔画包 */

static int s_tests = 0;
static int s_fails = 0;

#define CHECK(cond, msg) do {                                              \
    s_tests++;                                                             \
    if (!(cond)) { s_fails++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* 记录型绘制后端：只统计，不真正画 */
typedef struct {
    int line, circle, rect, dot;
    int min_x, min_y, max_x, max_y;
    int first_line[4];
    int have_first;
} rec_t;

static void note(rec_t *r, int x, int y) {
    if (x < r->min_x) r->min_x = x;
    if (y < r->min_y) r->min_y = y;
    if (x > r->max_x) r->max_x = x;
    if (y > r->max_y) r->max_y = y;
}
static void on_line(void *u, int x1, int y1, int x2, int y2) {
    rec_t *r = (rec_t *)u;
    r->line++;
    note(r, x1, y1); note(r, x2, y2);
    if (!r->have_first) {
        r->first_line[0] = x1; r->first_line[1] = y1;
        r->first_line[2] = x2; r->first_line[3] = y2;
        r->have_first = 1;
    }
}
static void on_circle(void *u, int cx, int cy, int rad) {
    rec_t *r = (rec_t *)u; r->circle++;
    note(r, cx - rad, cy - rad); note(r, cx + rad, cy + rad);
}
static void on_rect(void *u, int x1, int y1, int x2, int y2) {
    rec_t *r = (rec_t *)u; r->rect++;
    note(r, x1, y1); note(r, x2, y2);
}
static void on_dot(void *u, int cx, int cy, int rad) {
    rec_t *r = (rec_t *)u; r->dot++;
    note(r, cx - rad, cy - rad); note(r, cx + rad, cy + rad);
}
static wl_sink_t rec_sink(rec_t *r) {
    wl_sink_t s;
    memset(r, 0, sizeof(*r));
    r->min_x = r->min_y = 100000;
    r->max_x = r->max_y = -100000;
    s.line = on_line; s.circle = on_circle;
    s.rect = on_rect; s.dot = on_dot;
    s.user = r;
    return s;
}

static void test_version_and_assets(void) {
    int i, total = 0;
    printf("[1] 版本号 + 10 词真实资产全部可解析\n");
    CHECK(strcmp(wordline_version(), "wordline-v1") == 0, "版本号应为 wordline-v1");
    CHECK(WORD_P0_COUNT == 10, "P0 应为 10 词");
    CHECK(WORD_P0_STROKE_BYTES == WORDLINE_STROKE_BYTES, "每笔字节数应与解释器一致");

    for (i = 0; i < WORD_P0_COUNT; ++i) {
        const word_p0_t *w = &kWordP0[i];
        int n = wordline_validate(w->strokes, w->len);
        CHECK(n == w->stroke_count, "validate 笔数应与元数据一致");
        CHECK(w->len == WORDLINE_HEADER_BYTES
                        + WORDLINE_STROKE_BYTES * w->stroke_count,
              "len 应等于 3+5*笔数");
        CHECK(wordline_stroke_count(w->strokes, w->len) == w->stroke_count,
              "stroke_count 读取应一致");
        total += n;
    }
    CHECK(total == 149, "P0 合计应为 149 笔");
}

static void test_draw_all(void) {
    int i, total_strokes = 0, total_prims = 0;
    wl_box_t box = { 40, 60, 160, 160 };   /* 真机 160×160 线图区 */
    printf("[2] 全量绘制：图元数=笔数、坐标都落在 box 内\n");
    for (i = 0; i < WORD_P0_COUNT; ++i) {
        const word_p0_t *w = &kWordP0[i];
        rec_t r; wl_sink_t s = rec_sink(&r);
        int n = wordline_draw(w->strokes, w->len, &box, &s);
        CHECK(n == w->stroke_count, "draw 应返回该词笔数");
        CHECK(r.line + r.circle + r.rect + r.dot == n,
              "每笔恰好产生 1 个图元");
        CHECK(r.min_x >= box.x && r.max_x <= box.x + box.w,
              "x 坐标应落在 box 内");
        CHECK(r.min_y >= box.y && r.max_y <= box.y + box.h,
              "y 坐标应落在 box 内");
        total_strokes += n;
        total_prims += r.line + r.circle + r.rect + r.dot;
    }
    CHECK(total_strokes == 149 && total_prims == 149, "合计 149 笔 / 149 图元");
}

static void test_coord_mapping(void) {
    rec_t r; wl_sink_t s;
    /* 单笔 LINE(0,0,200,200) 的手工流 */
    static const uint8_t one[] = {
        0x57, 0x4C, 0x01,
        0x01, 0x00, 0x00, 0xC8, 0xC8
    };
    wl_box_t b = { 10, 20, 100, 100 };
    printf("[3] 坐标映射：0..200 → box，四舍五入\n");
    s = rec_sink(&r);
    CHECK(wordline_draw(one, sizeof(one), &b, &s) == 1, "单笔应绘制成功");
    CHECK(r.first_line[0] == 10 && r.first_line[1] == 20, "起点应映射到 box 左上");
    CHECK(r.first_line[2] == 110 && r.first_line[3] == 120, "终点应映射到 box 右下");

    /* 等比：box 恰好 200×200 时坐标不变 */
    { wl_box_t sq = { 0, 0, 200, 200 };
      s = rec_sink(&r);
      CHECK(wordline_draw(one, sizeof(one), &sq, &s) == 1, "等比 box 应成功");
      CHECK(r.first_line[2] == 200 && r.first_line[3] == 200, "200×200 时坐标不变");
    }
}

static void test_all_opcodes(void) {
    /* 4 种 opcode 各一笔 */
    static const uint8_t ops[] = {
        0x57, 0x4C, 0x04,
        0x01, 0x0A, 0x14, 0x64, 0x64,   /* LINE   */
        0x02, 0x64, 0x64, 0x1E, 0x00,   /* CIRCLE */
        0x03, 0x0A, 0x0A, 0x5A, 0x5A,   /* RECT   */
        0x04, 0x64, 0x64, 0x05, 0x00    /* DOT    */
    };
    wl_box_t b = { 0, 0, 200, 200 };
    rec_t r; wl_sink_t s = rec_sink(&r);
    printf("[4] 四种 opcode 各调用对应回调一次\n");
    CHECK(wordline_draw(ops, sizeof(ops), &b, &s) == 4, "4 笔应全部绘制");
    CHECK(r.line == 1 && r.circle == 1 && r.rect == 1 && r.dot == 1,
          "LINE/CIRCLE/RECT/DOT 各 1 次");
    CHECK(r.first_line[0] == 10 && r.first_line[1] == 20, "LINE 参数解析正确");

    /* 回调为 NULL 的图元应被跳过，其余照常 */
    { wl_sink_t only_line; rec_t r2;
      only_line = rec_sink(&r2);
      only_line.circle = NULL; only_line.rect = NULL; only_line.dot = NULL;
      CHECK(wordline_draw(ops, sizeof(ops), &b, &only_line) == 4,
            "NULL 回调不应导致失败");
      CHECK(r2.line == 1 && r2.circle == 0 && r2.rect == 0 && r2.dot == 0,
            "只计数 line 回调");
    }
}

static void test_reject_bad_data(void) {
    static const uint8_t ok[] = { 0x57, 0x4C, 0x01, 0x01, 0, 0, 10, 10 };
    static const uint8_t bad_magic[] = { 0x00, 0x4C, 0x01, 0x01, 0, 0, 10, 10 };
    static const uint8_t bad_len[]   = { 0x57, 0x4C, 0x02, 0x01, 0, 0, 10, 10 };
    static const uint8_t bad_op[]    = { 0x57, 0x4C, 0x01, 0x09, 0, 0, 10, 10 };
    static const uint8_t trunc[]     = { 0x57, 0x4C };
    wl_box_t b = { 0, 0, 200, 200 };
    rec_t r; wl_sink_t s = rec_sink(&r);
    printf("[5] 坏数据：返回错误码且一笔不画\n");
    CHECK(wordline_validate(NULL, 8) == WL_ERR_LENGTH, "NULL 应报 LENGTH");
    CHECK(wordline_validate(trunc, sizeof(trunc)) == WL_ERR_LENGTH, "截断应报 LENGTH");
    CHECK(wordline_validate(bad_magic, sizeof(bad_magic)) == WL_ERR_MAGIC, "魔数错应报 MAGIC");
    CHECK(wordline_validate(bad_len, sizeof(bad_len)) == WL_ERR_LENGTH, "count/长度不符应报 LENGTH");
    CHECK(wordline_validate(bad_op, sizeof(bad_op)) == WL_ERR_OPCODE, "非法 opcode 应报 OPCODE");
    CHECK(wordline_draw(bad_op, sizeof(bad_op), &b, &s) == WL_ERR_OPCODE,
          "坏数据应整笔拒绝");
    CHECK(r.line + r.circle + r.rect + r.dot == 0, "坏数据不应产生任何图元");
    CHECK(wordline_draw(ok, sizeof(ok), NULL, &s) == WL_ERR_ARG, "空 box 应报 ARG");
    CHECK(wordline_draw(ok, sizeof(ok), &b, NULL) == WL_ERR_ARG, "空 sink 应报 ARG");
    CHECK(wordline_stroke_count(bad_magic, sizeof(bad_magic)) == 0,
          "坏数据 stroke_count 应为 0");
}

int main(void) {
    printf("==== wordline 解释器单测 ====\n");
    test_version_and_assets();
    test_draw_all();
    test_coord_mapping();
    test_all_opcodes();
    test_reject_bad_data();
    printf("==============================\n");
    printf("用例 %d，失败 %d → %s\n", s_tests, s_fails,
           s_fails == 0 ? "ALL PASS" : "FAILED");
    return s_fails == 0 ? 0 : 1;
}
