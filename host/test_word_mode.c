/* test_word_mode.c —— 纯逻辑状态机 PC 单测（无需 ESP-IDF / LVGL）
 *
 * 编译运行：
 *   gcc -std=c99 -Wall -Wextra -Werror -I../firmware/components/core/services/include \
 *       test_word_mode.c ../firmware/components/core/services/src/word_mode.c -o test_word_mode
 *   ./test_word_mode
 *
 * 覆盖 PRD §6 全部状态转移 + 边界：词序回绕、动作队列、长按、
 * uint32 时间回绕、非法按键、定时幂等。
 *
 * 计时基准说明：word_mode_key() 内部 arm() 用的是「最近一次 tick 的 now_ms」
 * （init 之后为 0），所以先 key 再 tick 时，deadline = 0 + hold_ms。
 */
#include <stdio.h>
#include "services/word_mode.h"

static int s_tests = 0;
static int s_fails = 0;

#define CHECK(cond, msg) do {                                              \
    s_tests++;                                                             \
    if (!(cond)) { s_fails++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* 排空队列，返回动作个数，最后一个动作放入 *last */
static int drain(word_action_t *last) {
    int n = 0;
    word_action_t a;
    while ((a = word_mode_poll()) != WM_ACT_NONE) {
        n++;
        if (last) *last = a;
    }
    return n;
}

static void test_init(void) {
    word_action_t last = WM_ACT_NONE;
    printf("[1] init 首次进入不推进词序\n");
    word_mode_init(10, 0);
    CHECK(drain(&last) == 1, "init 应只产生 1 个动作");
    CHECK(last == WM_ACT_SHOW, "init 动作应为 SHOW");
    CHECK(word_mode_index() == 0, "首次词序应为 0");
    CHECK(word_mode_screen() == WM_SCREEN_SHOW, "首次应为 SHOW 屏");
    CHECK(word_mode_poll() == WM_ACT_NONE, "队列排空后应返回 NONE");

    word_mode_init(10, 7);
    drain(NULL);
    CHECK(word_mode_index() == 7, "应恢复上次词序 7");
    word_mode_init(10, 25);
    drain(NULL);
    CHECK(word_mode_index() == 5, "越界词序应取模为 5");
    word_mode_init(10, -3);
    drain(NULL);
    CHECK(word_mode_index() == 0, "负词序应归零");
    word_mode_init(0, 0);
    drain(NULL);
    CHECK(word_mode_index() == 0, "count=0 应安全退化为 1");
}

static void test_known_path(void) {
    printf("[2] UP=认识 → KNOWN → 1.5s → NEXT(词序+1)\n");
    word_mode_init(10, 2);
    drain(NULL);
    word_mode_key(WM_KEY_UP, false);
    CHECK(word_mode_screen() == WM_SCREEN_KNOWN, "UP 应进入 KNOWN 屏");
    CHECK(word_mode_poll() == WM_ACT_KNOWN, "UP 应产生 KNOWN 动作");
    CHECK(word_mode_poll() == WM_ACT_NONE, "UP 只产生 1 个动作");

    /* key 在 tick 之前，arm 基准 = init 后的 0，故 deadline = 1500ms */
    word_mode_tick(1000);
    CHECK(word_mode_poll() == WM_ACT_NONE, "未到期不应有动作");
    word_mode_tick(1499);
    CHECK(word_mode_poll() == WM_ACT_NONE, "1499ms 不应跳转");
    word_mode_tick(1500);
    CHECK(word_mode_poll() == WM_ACT_NEXT, "到期应产生 NEXT");
    CHECK(word_mode_poll() == WM_ACT_SHOW, "到期应产生 SHOW 重绘");
    CHECK(word_mode_index() == 3, "认识后词序 2→3");
    CHECK(word_mode_screen() == WM_SCREEN_SHOW, "到期应回 SHOW 屏");
}

static void test_unknown_path(void) {
    printf("[3] DOWN=不记得 → UNKNOWN+SLOW → 3s 回主屏、词序不变\n");
    word_mode_init(10, 4);
    drain(NULL);
    word_mode_key(WM_KEY_DOWN, false);
    CHECK(word_mode_screen() == WM_SCREEN_UNKNOWN, "DOWN 应进入 UNKNOWN 屏");
    CHECK(word_mode_poll() == WM_ACT_UNKNOWN, "DOWN 首个动作应为 UNKNOWN");
    CHECK(word_mode_poll() == WM_ACT_SLOW, "DOWN 次个动作应为 SLOW");
    CHECK(word_mode_index() == 4, "不记得不应推进词序");

    word_mode_key(WM_KEY_OK, false);       /* ⑥ OK 再慢速听一次，重新计时 */
    CHECK(word_mode_poll() == WM_ACT_SLOW, "UNKNOWN 下 OK 应再播 SLOW");
    CHECK(word_mode_screen() == WM_SCREEN_UNKNOWN, "再听仍留在 UNKNOWN 屏");

    /* UNKNOWN 停留 WM_HOLD_UNKNOWN_MS(3000ms) 到期回主屏 */
    word_mode_tick(0);
    word_mode_tick(2999);
    CHECK(word_mode_poll() == WM_ACT_NONE, "2999ms 不应回主屏");
    word_mode_tick(3000);
    CHECK(word_mode_poll() == WM_ACT_SHOW, "到期应回主屏重绘");
    CHECK(word_mode_screen() == WM_SCREEN_SHOW, "到期应回 SHOW 屏");
    CHECK(word_mode_index() == 4, "UNKNOWN 回主屏后词序仍不变");
    word_mode_tick(9999);
    CHECK(word_mode_poll() == WM_ACT_NONE, "定时触发一次后不应重复");
}

static void test_replay_and_skip(void) {
    printf("[4] OK短按=再听 / 任意键跳过结果\n");
    word_mode_init(10, 0);
    drain(NULL);
    word_mode_key(WM_KEY_OK, false);
    CHECK(word_mode_screen() == WM_SCREEN_LISTEN, "SHOW 下 OK 应进 LISTEN 屏");
    CHECK(word_mode_poll() == WM_ACT_REPLAY, "应产生 REPLAY 动作");
    word_mode_tick(0);
    word_mode_tick(1200);
    CHECK(word_mode_screen() == WM_SCREEN_SHOW, "LISTEN 到期应回 SHOW");
    CHECK(word_mode_poll() == WM_ACT_SHOW, "回 SHOW 应重绘");

    word_mode_key(WM_KEY_UP, false);       /* 进 KNOWN */
    drain(NULL);
    word_mode_key(WM_KEY_DOWN, false);     /* 任意键立即跳过 */
    CHECK(word_mode_poll() == WM_ACT_NEXT, "KNOWN 下任意键应立即 NEXT");
    CHECK(word_mode_poll() == WM_ACT_SHOW, "NEXT 后应 SHOW");
    CHECK(word_mode_index() == 1, "跳过后词序 0→1");
}

static void test_menu_request(void) {
    printf("[5] OK长按=MENU_REQUEST，UP/DOWN 长按忽略\n");
    word_mode_init(10, 3);
    drain(NULL);
    word_mode_key(WM_KEY_OK, true);
    CHECK(word_mode_poll() == WM_ACT_MENU_REQUEST, "长按 OK 应请求菜单");
    CHECK(word_mode_screen() == WM_SCREEN_SHOW, "菜单不应改变状态机 screen");
    CHECK(word_mode_index() == 3, "菜单请求不应推进词序");

    word_mode_key(WM_KEY_UP, true);
    word_mode_key(WM_KEY_DOWN, true);
    CHECK(word_mode_poll() == WM_ACT_NONE, "UP/DOWN 长按应被忽略");

    /* 任意屏长按 OK 都能呼出菜单 */
    word_mode_key(WM_KEY_UP, false);       /* KNOWN */
    drain(NULL);
    word_mode_key(WM_KEY_OK, true);
    CHECK(word_mode_poll() == WM_ACT_MENU_REQUEST, "KNOWN 屏长按 OK 也能呼菜单");
}

static void test_wrap_and_timing(void) {
    printf("[6] 词序回绕 / uint32 回绕 / 非法按键 / 定时幂等\n");
    word_mode_init(10, 9);
    drain(NULL);
    word_mode_key(WM_KEY_UP, false);
    drain(NULL);
    word_mode_tick(1000);
    word_mode_tick(2500);
    CHECK(word_mode_index() == 0, "词序 9 应回绕到 0");

    /* uint32 回绕附近的到期判断（deadline 与 now 同时跨越 2^32） */
    word_mode_init(10, 0);
    drain(NULL);
    word_mode_tick(0xFFFFFF00u);
    word_mode_key(WM_KEY_UP, false);       /* arm 基准 = 0xFFFFFF00 */
    drain(NULL);
    word_mode_tick(0xFFFFFF00u + 1500u);  /* 跨越回绕点 */
    CHECK(word_mode_poll() == WM_ACT_NEXT, "uint32 回绕后定时仍应生效");

    /* 非法按键索引被忽略 */
    word_mode_init(10, 0);
    drain(NULL);
    word_mode_key(99, false);
    CHECK(word_mode_poll() == WM_ACT_NONE, "非法按键索引应被忽略");

    /* 定时只触发一次（disarm 生效，幂等） */
    word_mode_key(WM_KEY_UP, false);
    drain(NULL);
    word_mode_tick(0);
    word_mode_tick(1500);
    CHECK(word_mode_poll() == WM_ACT_NEXT, "到期应产生 NEXT");
    CHECK(word_mode_poll() == WM_ACT_SHOW, "到期应产生 SHOW");
    word_mode_tick(9999);
    CHECK(word_mode_poll() == WM_ACT_NONE, "定时触发一次后不应重复");
}

int main(void) {
    printf("==== word_mode 纯逻辑单测 ====\n");
    test_init();
    test_known_path();
    test_unknown_path();
    test_replay_and_skip();
    test_menu_request();
    test_wrap_and_timing();
    printf("==============================\n");
    printf("用例 %d，失败 %d → %s\n", s_tests, s_fails,
           s_fails == 0 ? "ALL PASS" : "FAILED");
    return s_fails == 0 ? 0 : 1;
}
