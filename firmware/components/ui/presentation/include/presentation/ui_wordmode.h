/* ui_wordmode.h —— 单词模式 7 屏 LVGL 表现层（P0）
 *
 * 接口按 p0-prd.md §7 定稿签名，不擅自改动。
 * 依赖：LVGL 9.5、services(word_mode/wordline/adpcm/audio_service)、资产头。
 *
 * 铁律（见 PRD §11 契约测试）：
 *   - 禁用 lv_canvas / lv_image / 全屏位图；线图在 LV_EVENT_DRAW_MAIN 里逐笔直绘；
 *   - 单词模式内部全图标，不显示单词文本、音标、中文（孩子不识字）；
 *   - 纸白底 #fbfaf6 + 深墨 #202a33，与深色小智对话态形成「切换到学习」的直觉区分。
 */
#pragma once
#include "lvgl.h"
#include "services/word_mode.h"
#include "services/audio_service.h"
#include "presentation/ui_play_key.h"
#include "hal/hal_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 长按 OK 时请求 app 呼出菜单 ②′（菜单由既有 ui_plays 覆盖实现） */
typedef void (*ui_wordmode_menu_cb_t)(void *user);

/* 注入音频服务指针：必须在 ui_wordmode_open() 之前调用。
 * 抽成 setter 是为了不改动 PRD §7 已定稿的 open() 签名。 */
void ui_wordmode_set_audio(audio_service_t *svc);

/* 创建单词模式根屏并接管显示（需 LVGL 锁） */
void ui_wordmode_open(ui_wordmode_menu_cb_t on_menu, void *user);

/* 按状态机的 screen 重绘一屏；每次 lv_obj_delete_async(old) + 建新对象 */
void ui_wordmode_render(wm_screen_t screen, int word_index);

/* 手势 → 状态机。需 LVGL 锁。
 *   PRESS + UP/DOWN = 图案反馈（跟手）；CLICK + OK = 短按；LONG + OK = 长按呼菜单。
 *   app 层调用本函数后应立即 wordmode_app_pump() 消费动作队列。 */
void ui_wordmode_key(ui_play_key_t key, hal_btn_event_t event);

/* 立即中止当前发音（OK 短按重播、退出单词模式时调用） */
void ui_wordmode_stop_audio(void);

/* 非阻塞分块解码：由 app 层的 LVGL 定时器（50 ms）调用，每次推 256 B。
 * 返回累计解码样本数。这样 audio_service_play() 不会长时间占住 LVGL 任务
 * （PRD §8「若阻塞放到 plays worker」的等价轻量实现）。 */
uint32_t ui_wordmode_audio_tick(void);
bool ui_wordmode_audio_active(void);

void ui_wordmode_close(void);
bool ui_wordmode_active(void);

/* 从 p0_audio.h 取 ADP1 段，校验头后启动流式播放。
 * 成功返回首批解码样本数；失败返回负值（头损坏时静默跳过，不显示失败符号）。
 * word 为 NULL 时按当前词索引取。 */
int ui_wordmode_play(const char *word, bool slow);

#ifdef __cplusplus
}
#endif
