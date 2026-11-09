/* wordline.h —— 笔画指令解释器 wordline-v1（P0，纯逻辑层）
 *
 * 为什么存在：ESP32-C3 无 PSRAM、LVGL 池固定 40 KiB，一张 160×160 RGB565
 * 全屏位图约 50 KiB 放不下。所以 AI/备课侧不生成图片，只生成「笔画指令」：
 * 每笔 5 字节，一个词约 100~400 B，static const 直接映射 Flash、零 SRAM。
 * 设备端在 LV_EVENT_DRAW_MAIN 里逐笔直绘，禁用 lv_canvas。
 *
 * 二进制格式（由 tools/gen_lineart.py 生成，见 p0_pack.h）：
 *   [0] 0x57 'W'   [1] 0x4C 'L'   [2] stroke_count(1..255)
 *   之后 n 笔，每笔 5 字节 [op, a, b, c, d]：
 *     op=1 LINE   : x1=a, y1=b, x2=c, y2=d
 *     op=2 CIRCLE : cx=a, cy=b, r=c, d=0
 *     op=3 RECT   : x1=a, y1=b, x2=c, y2=d
 *     op=4 DOT    : cx=a, cy=b, r=c, d=0
 *   坐标是 0..200 的归一化值，曲线已在备课侧扁平成折线。
 *
 * 分层：本层不 include lvgl.h / esp 系列 / driver 目录下任何头文件，
 * 只做「解析 + 坐标映射」，通过 wl_sink_t 回调把几何图元交给 UI 层。
 * 因此可在 PC 上用 gcc 编译单测（见 host/test_wordline.c）。
 */
#pragma once
#include <stdint.h>

#define WORDLINE_VERSION      "wordline-v1"
#define WORDLINE_MAGIC0       0x57u   /* 'W' */
#define WORDLINE_MAGIC1       0x4Cu   /* 'L' */
#define WORDLINE_COORD_MAX    200     /* 备课侧归一化坐标上限 */
#define WORDLINE_STROKE_BYTES 5
#define WORDLINE_HEADER_BYTES 3

/* 画笔 opcode（与 p0_pack.h 生成端一致，改动需同步 tools/gen_lineart.py） */
enum {
    WL_OP_LINE   = 1,
    WL_OP_CIRCLE = 2,
    WL_OP_RECT   = 3,
    WL_OP_DOT    = 4
};

/* 错误码（wordline_validate / wordline_draw 的负返回值） */
enum {
    WL_ERR_ARG       = -1,  /* 空指针 / box 或 sink 非法 */
    WL_ERR_MAGIC     = -2,  /* 头两字节不是 "WL" */
    WL_ERR_LENGTH    = -3,  /* len 与 3+5*count 不符，或不足 3 字节 */
    WL_ERR_OPCODE    = -4   /* 出现 1..4 之外的 opcode */
};

/* 目标绘制区域（屏幕像素坐标，正方形为宜，保证圆不被拉成椭圆） */
typedef struct { int x, y, w, h; } wl_box_t;

/* 绘制后端：UI 层只需实现这 4 个回调；任一回调可为 NULL（该图元被跳过）。
 * 所有坐标已经映射到 box 内的屏幕像素坐标，回调无需再做变换。 */
typedef struct {
    void (*line)  (void *user, int x1, int y1, int x2, int y2);
    void (*circle)(void *user, int cx, int cy, int r);
    void (*rect)  (void *user, int x1, int y1, int x2, int y2);
    void (*dot)   (void *user, int cx, int cy, int r);
    void *user;
} wl_sink_t;

/* 校验一个词的笔画流是否合法。成功返回笔数(>=0)，失败返回 WL_ERR_*。 */
int wordline_validate(const uint8_t *data, uint16_t len);

/* 读取笔画数；非法流返回 0（不替代 validate 做完整性判断）。 */
uint8_t wordline_stroke_count(const uint8_t *data, uint16_t len);

/* 解析并绘制：把 0..200 归一化坐标线性映射到 box，逐笔调用 sink。
 * 成功返回实际绘制的笔数；失败返回 WL_ERR_*（此时不产生任何绘制）。 */
int wordline_draw(const uint8_t *data, uint16_t len,
                  const wl_box_t *box, const wl_sink_t *sink);

const char *wordline_version(void);
