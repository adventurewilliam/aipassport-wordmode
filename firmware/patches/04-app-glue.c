/* 04-app-glue.c —— app.c / app_plays.c / ui_plays.c 的胶水代码（可直接粘贴）
 *
 * 这不是可一键 git apply 的 diff：上游这三个文件体量大且随版本演进，
 * 行号会漂。下面按「函数 + 插入位置 + 调用点」给出，集成时按真实源码落位。
 * 所有函数都只调用新增模块的公开接口，不触碰上游内部静态变量。
 *
 * 依赖头（加到对应文件顶部）：
 *   app.c        : #include "app/wordmode_app.h"
 *                  #include "services/audio_service.h"
 *   app_plays.c  : #include "app/wordmode_app.h"
 *   ui_plays.c   : 无新增（只改菜单绘制逻辑）
 */

/* ============================================================================
 * A. app.c —— 开机注册 + 菜单选择分支 + 长按 OK 呼菜单（C1）
 * ==========================================================================*/

/* A1. 在 app_run() 里，s_kv / s_audio 等适配器创建完成之后，加一次注册。
 *     具体位置：紧跟 app_plays_init(&cfg) 成功之后。 */
#if 0
{
    wordmode_app_config_t wm_cfg = {
        .kv              = s_kv,                 /* 既有 hal_kv_t * */
        .audio           = &s_audio,            /* 既有 audio_service_t */
        .stop_xiaozhi    = glue_stop_xiaozhi,
        .xiaozhi_stopped = glue_xiaozhi_stopped,
        .resume_xiaozhi  = glue_resume_xiaozhi,
        .open_menu       = glue_open_menu,
        .close_menu      = glue_close_menu,
        .user            = NULL,
    };
    wordmode_app_init(&wm_cfg);
}
#endif

/* A2. 下面 6 个回调把新增模块与上游现成能力接起来，放在 app.c 文件作用域。 */
static void glue_stop_xiaozhi(void *user) {
    (void)user;
    xiaozhi_stop();                 /* 非阻塞取消，安全在 LVGL 锁内调用 */
}
static bool glue_xiaozhi_stopped(void *user) {
    (void)user;
    return xiaozhi_stopped();       /* 仅在网络/音频资源全部释放后为 true */
}
static void glue_resume_xiaozhi(void *user) {
    (void)user;
    xiaozhi_resume();               /* 从在线待机重新开一轮对话 */
}
static void glue_open_menu(void *user) {
    (void)user;
    ui_plays_set_wordmode_context(wordmode_app_active());
    ui_plays_menu();                /* 既有接口 */
}
static void glue_close_menu(void *user) {
    (void)user;
    ui_plays_close();
}

/* A3. 在既有的 play select 回调里增加一个 case。
 *     顺序即菜单顺序，UI_PLAY_WORDMODE 已在枚举首位。 */
#if 0
static bool on_play_select(ui_play_id_t play, void *user) {
    switch (play) {
    case UI_PLAY_WORDMODE:
        return app_plays_open_wordmode(0);   /* best 参数在单词模式不用 */
    /* ... 其余原有 case 原样保留 ... */
    }
}
#endif

/* A4. 长按 OK 呼菜单（C1 定稿）：在既有 on_button 回调里，
 *     HAL_BTN_LONG 且 index==2 的分支追加一行。
 *     注意：上游 XIAOZHI_BUILD.txt 第 4 条本就是「对话页长按确认打开菜单」，
 *     这里只是让同一动作在单词模式内也生效；长按后的松手不重复触发，
 *     由表现层 s_suppress_click 与上游既有逻辑共同保证。 */
#if 0
if (e == HAL_BTN_LONG && index == 2) {
    if (wordmode_app_active()) {
        ui_plays_set_wordmode_context(true);
        ui_plays_menu();
    } else {
        ui_plays_set_wordmode_context(false);
        ui_plays_menu();            /* 复用上游既有菜单触发，不新增长按键 */
    }
}
#endif

/* A5. 菜单里选到「返回小智」时（②′ 首项），调用：
 *        wordmode_app_on_menu_exit_selected();
 *     它会写 NVS、关 UI、resume 小智。 */

/* A6. LVGL 定时器（既有 50 ms tick）里无条件追加一行：
 *        wordmode_app_pump(now_ms);
 *     未进入单词模式时该函数立即返回，开销可忽略。
 *     now_ms 用 esp_timer_get_time()/1000 或既有单调时钟即可。 */


/* ============================================================================
 * B. app_plays.c —— 3 个入口 + 裸手势转发
 * ==========================================================================*/

bool app_plays_open_wordmode(int best) {
    (void)best;
    if (!wordmode_app_enter()) return false;
    /* 真正的 ui_wordmode_open() 在 wordmode_app_pump() 里等
     * xiaozhi_stopped() 完成后执行，此处不阻塞 LVGL 锁。 */
    return true;
}
bool app_plays_wordmode_active(void) {
    return wordmode_app_active();
}
void app_plays_wordmode_button(int index, hal_btn_event_t event) {
    if (!wordmode_app_active()) return;
    wordmode_app_button(index, event);
}

/* B2. 在既有 app_plays_button() 的最前面加一个分流：
 *     单词模式激活时，把完整裸手势（含 PRESS）转给单词模式并 return，
 *     避免既有 clicker / xiaozhi 的手势处理重复消费同一次按键。 */
#if 0
void app_plays_button(int index, hal_btn_event_t event) {
    if (wordmode_app_active()) {
        wordmode_app_button(index, event);
        return;
    }
    /* ... 原有逻辑原样保留 ... */
}
#endif


/* ============================================================================
 * C. ui_plays.c —— 菜单首项默认高亮 + 单词模式内动态首项
 * ==========================================================================*/

/* C1. 文件作用域加一个静态标记。 */
static bool s_wordmode_context = false;

void ui_plays_set_wordmode_context(bool in_wordmode) {
    s_wordmode_context = in_wordmode;
}

/* C2. 在菜单项标签数组 / 绘制处，把首项文字按上下文切换。
 *     其余 6 项与原有「返回」项保持不变，总数仍为 7。 */
#if 0
static const char *menu_label(int i) {
    if (i == 0) return s_wordmode_context ? "返回小智" : "单词模式";
    /* ... 原有 1..6 项文字 ... */
}
#endif

/* C3. 菜单打开时把焦点固定到第 1 项（i==0），保证「长按 OK + OK」两步进入。
 *     若原有实现有滚动窗口，确保 s_focus 初值为 0 即可，无需改其它项。 */


/* ============================================================================
 * D. 需要的 include（按上游实际路径调整）
 * ==========================================================================*/
#include "app/wordmode_app.h"
#include "presentation/ui_plays.h"
#include "xiaozhi.h"
#include "services/audio_service.h"
