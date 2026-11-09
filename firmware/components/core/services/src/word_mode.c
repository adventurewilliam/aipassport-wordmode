/* word_mode.c —— 小朋友单词模式 · 纯逻辑状态机（P0）
 *
 * 编译约束：只 include C99 标准头，不依赖 LVGL / ESP-IDF / driver。
 *   gcc -std=c99 -Wall -Wextra -Werror -c word_mode.c  必须零告警通过。
 *
 * 状态转移（PRD §6 的精确落地版）：
 *
 *   init ──▶ SHOW ──UP────▶ KNOWN ──1.5s/任意键──▶ NEXT ──▶ SHOW(词序+1)
 *              │
 *              ├──DOWN──▶ UNKNOWN ──慢速播完/UP/DOWN──▶ SHOW(词序不变)
 *              │              └──OK短按──▶ SLOW(再听一次), 仍 UNKNOWN
 *              ├──OK短按─▶ LISTEN ──1.2s/任意键──▶ SHOW
 *              └──OK长按─▶ MENU_REQUEST（交给 app，screen 保持不变）
 *
 *   任意屏 OK 长按都发 MENU_REQUEST；菜单由既有 ui_plays 覆盖实现，
 *   app 取消菜单后直接用 ui_wordmode_render(screen, index) 重绘即可，
 *   状态机无需 resume 接口（保持 PRD §7 签名不变）。
 *
 * 设计取舍：
 *   - "认识"才推进词序；"不记得 / 再听"重复当前词 —— 符合幼儿学习节奏。
 *   - P0 无播放完成回调（PRD 签名未定义），故 ④/⑥ 用定时兜底回主屏；
 *     P1 接 NAS/在线链路时换成播放完成事件即可，本文件改动极小。
 */
#include "services/word_mode.h"

/* ---- 停留时长（ms），真机可在此一处微调 ---- */
#define WM_HOLD_KNOWN_MS    1500u  /* ⑤ 认识反馈：1.5 s 自动进下一词 */
#define WM_HOLD_LISTEN_MS   1200u  /* ④ 跟读占位：P0 不采音 */
#define WM_HOLD_UNKNOWN_MS  3000u  /* ⑥ 不记得：覆盖最长慢速发音 + 余量 */

/* 动作队列：静态环形，零 malloc。一次按键最多 2 个动作，8 槽足够。 */
#define WM_QUEUE_CAP 8

typedef struct {
    int           count;
    int           index;
    wm_screen_t   screen;
    bool          started;        /* init 之后为 true */
    bool          timed;          /* 是否有待到期定时 */
    uint32_t      deadline_ms;
    uint32_t      last_now_ms;    /* word_mode_key 的计时基准 */
    word_action_t queue[WM_QUEUE_CAP];
    unsigned      head;
    unsigned      length;
} wm_state_t;

static wm_state_t s;

/* ---------------- 动作队列 ---------------- */
static void q_clear(void) {
    s.head = 0;
    s.length = 0;
}

static void q_push(word_action_t a) {
    if (s.length >= WM_QUEUE_CAP) {
        return; /* 溢出丢弃最新动作：宁可少播一次，不覆盖已有反馈 */
    }
    s.queue[(s.head + s.length) % WM_QUEUE_CAP] = a;
    s.length++;
}

word_action_t word_mode_poll(void) {
    word_action_t a;
    if (!s.started || s.length == 0) {
        return WM_ACT_NONE;
    }
    a = s.queue[s.head];
    s.head = (s.head + 1) % WM_QUEUE_CAP;
    s.length--;
    return a;
}

/* ---------------- 定时 ---------------- */
static void arm(uint32_t hold_ms) {
    s.deadline_ms = s.last_now_ms + hold_ms;
    s.timed = true;
}

static void disarm(void) {
    s.timed = false;
}

/* 无符号回绕安全的到期判断 */
static bool expired(uint32_t now_ms) {
    return s.timed && ((int32_t)(now_ms - s.deadline_ms) >= 0);
}

/* ---------------- 屏切换 ---------------- */
static void enter_show(bool advance) {
    if (advance && s.count > 0) {
        s.index = (s.index + 1) % s.count;
    }
    s.screen = WM_SCREEN_SHOW;
    disarm();
    q_push(WM_ACT_SHOW); /* app 收到后：渲染线图 + 播 normal + 写 NVS 词序 */
}

static void request_next(void) {
    disarm();
    q_push(WM_ACT_NEXT);
    enter_show(true);
}

/* ---------------- 公共接口 ---------------- */
void word_mode_init(int word_count, int saved_index) {
    s.count   = (word_count > 0) ? word_count : 1;
    s.index   = (saved_index < 0) ? 0 : (saved_index % s.count);
    s.screen  = WM_SCREEN_SHOW;
    s.started = true;
    s.timed   = false;
    s.deadline_ms = 0;
    s.last_now_ms = 0;
    q_clear();
    /* 首次进入渲染「上次学到的词」，不推进词序 */
    q_push(WM_ACT_SHOW);
}

void word_mode_tick(uint32_t now_ms) {
    if (!s.started) {
        return;
    }
    s.last_now_ms = now_ms;
    if (!expired(now_ms)) {
        return;
    }
    switch (s.screen) {
    case WM_SCREEN_KNOWN:
        request_next();
        break;
    case WM_SCREEN_LISTEN:
    case WM_SCREEN_UNKNOWN:
        enter_show(false);
        break;
    default:
        disarm();
        break;
    }
}

void word_mode_key(int index, bool long_press) {
    if (!s.started) {
        return;
    }

    /* 长按：只有 OK 有语义（呼出菜单）；UP/DOWN 长按一律忽略，防误触。
     * 不改变 screen：菜单是 app 层的覆盖层，取消后按当前 screen 重绘。 */
    if (long_press) {
        if (index == WM_KEY_OK) {
            q_push(WM_ACT_MENU_REQUEST);
        }
        return;
    }

    /* MENU 屏在本实现中不会由状态机进入（保留给 P1），仍做防御性处理 */
    if (s.screen == WM_SCREEN_MENU) {
        return;
    }

    switch (s.screen) {
    case WM_SCREEN_SHOW:
        if (index == WM_KEY_UP) {
            /* 认识（拇指）→ ⑤ 正反馈 */
            s.screen = WM_SCREEN_KNOWN;
            arm(WM_HOLD_KNOWN_MS);
            q_push(WM_ACT_KNOWN);
        } else if (index == WM_KEY_DOWN) {
            /* 不记得（问号）→ ⑥ 慢速重播，刻意无失败符号 */
            s.screen = WM_SCREEN_UNKNOWN;
            arm(WM_HOLD_UNKNOWN_MS);
            q_push(WM_ACT_UNKNOWN);
            q_push(WM_ACT_SLOW);
        } else if (index == WM_KEY_OK) {
            /* 再听一次 → ④ 跟读占位屏，播 normal */
            s.screen = WM_SCREEN_LISTEN;
            arm(WM_HOLD_LISTEN_MS);
            q_push(WM_ACT_REPLAY);
        }
        break;

    case WM_SCREEN_KNOWN:
        /* 任意键立即跳过结果，进下一词 */
        request_next();
        break;

    case WM_SCREEN_LISTEN:
        /* 任意键跳过跟读占位，回主屏 */
        enter_show(false);
        break;

    case WM_SCREEN_UNKNOWN:
        if (index == WM_KEY_OK) {
            /* 再慢速听一次，重新计时 */
            arm(WM_HOLD_UNKNOWN_MS);
            q_push(WM_ACT_SLOW);
        } else {
            /* UP / DOWN 立即回主屏，孩子不会卡住 */
            enter_show(false);
        }
        break;

    default:
        break;
    }
}

int word_mode_index(void) {
    return s.index;
}

wm_screen_t word_mode_screen(void) {
    return s.screen;
}
