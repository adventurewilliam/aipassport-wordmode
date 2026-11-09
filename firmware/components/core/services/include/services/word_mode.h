/* word_mode.h —— 小朋友单词模式 · 纯逻辑状态机（P0）
 *
 * 铁律（沿用底层仓库 doc/ARCHITECTURE.md 的六边形分层）：
 *   - 本层属于 services，只依赖 C99 标准库；
 *   - 不 include lvgl.h / esp 系列 / driver 目录下任何头文件；
 *   - 可在 PC 上用 gcc 编译并单测（见 host/test_word_mode.c）。
 *
 * 设计要点：状态机只输出「语义动作」(word_action_t)，由 app 层翻译成
 * 「渲染哪一屏 / 播哪段发音 / 写哪个 NVS 键」。这样协议、UI、素材变化
 * 都不会返工本文件。
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 动作：poll() 每次返回一个并出队；队列空时返回 WM_ACT_NONE。
 * 一次按键可产生多个动作（例如 DOWN = UNKNOWN + SLOW）。 */
typedef enum {
    WM_ACT_NONE = 0,
    WM_ACT_SHOW,          /* 进入主屏③：渲染当前词线图 + 播 normal 发音 + 持久化词序 */
    WM_ACT_REPLAY,        /* ④ 跟读占位屏：再播一次 normal */
    WM_ACT_SLOW,          /* ⑥ 不记得：播 *_slow 慢速发音 */
    WM_ACT_KNOWN,         /* ⑤ 认识 / 读对了：正反馈（大拇指 + 星星） */
    WM_ACT_UNKNOWN,       /* ⑥ 不记得：耳朵 + 循环箭头，不出现任何失败符号 */
    WM_ACT_NEXT,          /* 词序推进（仅「认识」路径会触发） */
    WM_ACT_MENU_REQUEST   /* 长按 OK：请求 app 呼出菜单②′（由 app 接管） */
} word_action_t;

/* 屏幕：app 层据此调 ui_wordmode_render()。MENU 由既有 ui_plays 菜单覆盖实现，
 * 状态机只记住「当前处于菜单上下文」，不自行绘制菜单。 */
typedef enum {
    WM_SCREEN_SHOW = 0,   /* ③ 看图 + 二选一 */
    WM_SCREEN_LISTEN,     /* ④ 跟读（P0 占位，不采音） */
    WM_SCREEN_KNOWN,      /* ⑤ 读对了 / 认识 */
    WM_SCREEN_UNKNOWN,    /* ⑥ 没读对 / 不记得 */
    WM_SCREEN_MENU        /* ②′ 菜单上下文 */
} wm_screen_t;

/* 按键索引，与 platform_lvgl_attach_buttons 的 prev/next/enter 一致 */
enum { WM_KEY_UP = 0, WM_KEY_DOWN = 1, WM_KEY_OK = 2 };

/* 初始化（每次进入单词模式调用一次）。
 *   word_count  : 词数，必须 > 0；非法值按 1 处理
 *   saved_index : 上次学到的词序（来自 hal_kv "wordmode:index"），越界自动取模
 * 首次进入只渲染 saved_index 对应的词，**不推进词序**（否则第 0 词永远看不到）。 */
void     word_mode_init(int word_count, int saved_index);

/* 喂一个按键。index 见 WM_KEY_*；long_press 为 true 表示长按 OK。
 *   - 长按只对 OK 有语义（呼出菜单）；UP/DOWN 长按一律忽略，防误触。
 *   - 长按后的松手不重复触发：该约束由 app 层转发裸手势时保证
 *     （沿用 XIAOZHI_BUILD.txt 第 6 条既有实现），本层不接收 RELEASE。 */
void     word_mode_key(int index, bool long_press);

/* 由 LVGL 定时器周期性调用（建议 50 ms）。now_ms 为单调毫秒时钟。
 * 驱动 ⑤ 1.5 s 自动进下一词、④/⑥ 自动回主屏。无符号回绕安全。
 * 注意：word_mode_key() 没有时间参数，内部以「最近一次 tick 的 now_ms」
 * 作为计时基准，因此调用方必须先启动 tick 再喂按键。 */
void     word_mode_tick(uint32_t now_ms);

/* 取出并清空一个待执行动作；队列空返回 WM_ACT_NONE。 */
word_action_t word_mode_poll(void);

int          word_mode_index(void);
wm_screen_t  word_mode_screen(void);

#ifdef __cplusplus
}
#endif
