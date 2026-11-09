/* wordmode_app.c —— 单词模式编排层（P0，新增文件）
 *
 * 只依赖 HAL + services + presentation 的公开头，不直接 include xiaozhi.h /
 * ui_plays.h / app.c 内部符号：对小智对话态的 stop/resume 全部经回调注入，
 * 因此本文件可以随独立仓库一起编译，集成时只需在 app.c 里补齐回调实现。
 *
 * NVS 键：wordmode:index（int32，词序）。读取失败/缺键按 0 处理。
 */
#include "app/wordmode_app.h"
#include "services/word_mode.h"
#include "presentation/ui_wordmode.h"
#include "p0_pack.h"
#include <string.h>

#define WM_KV_KEY "wordmode:index"

typedef enum {
    WM_APP_IDLE = 0,     /* 小智对话态 */
    WM_APP_ENTERING,     /* 已 stop_xiaozhi()，等 xiaozhi_stopped() */
    WM_APP_ACTIVE        /* 单词模式中 */
} wm_app_phase_t;

static wordmode_app_config_t s_cfg;
static wm_app_phase_t        s_phase = WM_APP_IDLE;
static bool                  s_inited;
static int                   s_saved_index;

/* ---------------- NVS 进度 ---------------- */
static int load_index(void) {
    int32_t v = 0;
    int out_len = 0;
    if (!s_cfg.kv) return 0;
    if (hal_kv_get(s_cfg.kv, WM_KV_KEY, &v, (int)sizeof(v), &out_len) == 0 &&
        out_len == (int)sizeof(v)) {
        return (int)v;
    }
    return 0;                 /* 缺键 / 损坏：从第 0 词开始 */
}
static void save_index(int index) {
    int32_t v = (int32_t)index;
    if (!s_cfg.kv) return;
    if ((int)v == s_saved_index) return;   /* 相同内容不重复写 Flash */
    if (hal_kv_set(s_cfg.kv, WM_KV_KEY, &v, (int)sizeof(v)) == 0) {
        s_saved_index = (int)v;
    }
}

/* ---------------- 动作 → 副作用 ---------------- */
static const char *word_at(int index) {
    if (index < 0 || index >= WORD_P0_COUNT) index = 0;
    return kWordP0[index].word;
}

static void request_menu(void *user) {
    (void)user;
    if (s_cfg.open_menu) s_cfg.open_menu(s_cfg.user);
}

static void apply_action(word_action_t act) {
    int idx = word_mode_index();
    switch (act) {
    case WM_ACT_SHOW:
        /* 主屏：渲染线图 + 播正常发音 + 持久化词序 */
        ui_wordmode_render(WM_SCREEN_SHOW, idx);
        (void)ui_wordmode_play(word_at(idx), false);
        save_index(idx);
        break;
    case WM_ACT_REPLAY:
        /* ④ 跟读占位：再播一次正常速 */
        ui_wordmode_render(WM_SCREEN_LISTEN, idx);
        (void)ui_wordmode_play(word_at(idx), false);
        break;
    case WM_ACT_SLOW:
        /* ⑥ 不记得：播预生成的慢速段（不做实时变速，无 FPU） */
        (void)ui_wordmode_play(word_at(idx), true);
        break;
    case WM_ACT_KNOWN:
        ui_wordmode_render(WM_SCREEN_KNOWN, idx);
        break;
    case WM_ACT_UNKNOWN:
        ui_wordmode_render(WM_SCREEN_UNKNOWN, idx);
        break;
    case WM_ACT_NEXT:
        /* 词序推进已在状态机内完成；紧随其后的 SHOW 会重绘并持久化 */
        break;
    case WM_ACT_MENU_REQUEST:
        /* 长按 OK：呼出 ②′；菜单是 app 层覆盖层，状态机 screen 不变 */
        if (s_cfg.open_menu) s_cfg.open_menu(s_cfg.user);
        break;
    case WM_ACT_NONE:
    default:
        break;
    }
}

static void drain_actions(void) {
    word_action_t a;
    while ((a = word_mode_poll()) != WM_ACT_NONE) {
        apply_action(a);
    }
}

/* ---------------- 公共接口 ---------------- */
bool wordmode_app_init(const wordmode_app_config_t *cfg) {
    if (!cfg) return false;
    s_cfg = *cfg;
    s_phase = WM_APP_IDLE;
    s_saved_index = 0;
    s_inited = true;
    return true;
}

bool wordmode_app_enter(void) {
    if (!s_inited || s_phase != WM_APP_IDLE) return false;
    s_saved_index = load_index();
    if (s_cfg.stop_xiaozhi) s_cfg.stop_xiaozhi(s_cfg.user);
    s_phase = WM_APP_ENTERING;
    return true;
}

void wordmode_app_exit(void) {
    if (s_phase == WM_APP_IDLE) return;
    if (s_phase == WM_APP_ACTIVE) {
        save_index(word_mode_index());
        ui_wordmode_close();
    }
    s_phase = WM_APP_IDLE;
    if (s_cfg.close_menu) s_cfg.close_menu(s_cfg.user);
    if (s_cfg.resume_xiaozhi) s_cfg.resume_xiaozhi(s_cfg.user);
}

bool wordmode_app_active(void)     { return s_phase == WM_APP_ACTIVE; }
bool wordmode_app_entering(void)   { return s_phase == WM_APP_ENTERING; }

void wordmode_app_on_menu_exit_selected(void) {
    wordmode_app_exit();
}

void wordmode_app_button(int index, hal_btn_event_t event) {
    ui_play_key_t key;
    if (s_phase != WM_APP_ACTIVE) return;
    if (index == 0)      key = UI_PLAY_UP;
    else if (index == 1) key = UI_PLAY_DOWN;
    else if (index == 2) key = UI_PLAY_OK;
    else return;
    ui_wordmode_key(key, event);
    drain_actions();       /* 图案反馈立即出，不等下一个 50 ms tick */
}

void wordmode_app_pump(uint32_t now_ms) {
    if (!s_inited) return;

    /* 等小智释放完资源再真正进入：两模式绝不共存 */
    if (s_phase == WM_APP_ENTERING) {
        bool stopped = s_cfg.xiaozhi_stopped
                       ? s_cfg.xiaozhi_stopped(s_cfg.user) : true;
        if (!stopped) return;
        word_mode_init(WORD_P0_COUNT, s_saved_index);
        ui_wordmode_set_audio(s_cfg.audio);
        ui_wordmode_open(request_menu, s_cfg.user);
        s_phase = WM_APP_ACTIVE;
        drain_actions();   /* 消费 init 产生的 SHOW */
        return;
    }

    if (s_phase != WM_APP_ACTIVE) return;
    word_mode_tick(now_ms);
    drain_actions();
    (void)ui_wordmode_audio_tick();   /* 推一块 ADP1，非阻塞 */
}
