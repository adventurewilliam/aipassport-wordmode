/* wordmode_app.h —— 单词模式编排层（P0，新增文件）
 *
 * 职责：把 services/word_mode.c 输出的「语义动作」翻译成副作用：
 *   UI 重绘、ADP1 发音、NVS 进度、呼出菜单、与小智对话态的资源交接。
 *
 * 为什么单独成层：底层仓库是六边形端口-适配器架构，app 层负责组装。
 * 把编排集中在一个新增文件，可以把「必须改动现有文件」的面积压到最小
 * （app_plays.h/.c、app.c、ui_plays.h/.c、2 个 CMakeLists，见 firmware/patches/）。
 *
 * 资源交接契约（PRD §10 步骤 7）：
 *   进入：stop_xiaozhi() → 轮询 xiaozhi_stopped() → 真正 ui_wordmode_open()
 *   退出：ui_wordmode_close() → 写 NVS → resume_xiaozhi()
 *   两模式绝不共存，靠交接释放编解码器/麦克风/套接字/TLS 腾出堆。
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hal/hal_button.h"
#include "hal/hal_kv.h"
#include "services/audio_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    hal_kv_t         *kv;                       /* 存 wordmode:index */
    audio_service_t  *audio;                    /* ADP1 播放 */
    void  (*stop_xiaozhi)(void *user);          /* → xiaozhi_stop() */
    bool  (*xiaozhi_stopped)(void *user);       /* → xiaozhi_stopped() */
    void  (*resume_xiaozhi)(void *user);        /* → xiaozhi_resume() */
    void  (*open_menu)(void *user);             /* → ui_plays_menu()，出 ②′ */
    void  (*close_menu)(void *user);            /* → ui_plays_close() */
    void *user;
} wordmode_app_config_t;

/* 开机调一次，注入依赖；不占用任何硬件资源 */
bool wordmode_app_init(const wordmode_app_config_t *cfg);

/* 从菜单选「单词模式」时调用：请求停小智，异步等资源释放后真正进入。
 * 返回 true 表示进入流程已启动；重复调用被忽略。 */
bool wordmode_app_enter(void);

/* 从菜单「返回小智」或 ②′ 退出时调用 */
void wordmode_app_exit(void);

bool wordmode_app_active(void);
bool wordmode_app_entering(void);   /* 正在等 xiaozhi_stopped() */

/* 裸手势转发（app_plays_wordmode_button 直接调用，需 LVGL 锁） */
void wordmode_app_button(int index, hal_btn_event_t event);

/* LVGL 定时器每 50 ms 调一次：驱动状态机定时、消费动作队列、推音频块。
 * now_ms 为单调毫秒时钟。 */
void wordmode_app_pump(uint32_t now_ms);

/* 菜单选择回调里调用：用户选了「返回小智」 */
void wordmode_app_on_menu_exit_selected(void);

#ifdef __cplusplus
}
#endif
