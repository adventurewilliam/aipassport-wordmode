# 设计：单词模式 —— P0 离线保底版

> 日期：2026-11-09
> 基线：`arraylee/ai-passport-xiaozhi-custom` **2.1.0-xiaozhi.18**（ESP-IDF 5.5.3 / ESP32-C3 / 8 MB Flash / **无 PSRAM** / LVGL 9.5）
> 上游文档：`/workspace/wordmode-plan.html`（框架与线稿 v0.3.1，已拍板）
> 交付资产：`/workspace/words/`（NAS 词表，P1 用）、`/workspace/p0-pack/`（P0 内置包，本版用）
> 范围：**P0 = 纯离线、10 词、7 屏、三键定型**。不接在线语音链路、不接 NAS、不做跟读评测。
> 目标：产出一版能烧进真机、让 6 岁小朋友直接按一按的最小闭环，用来验证「三键 + 两个图案」是否真的零门槛。
> 变更（**v1.1 · 方案 A 已落地**）：banana / milk / tree 按 bbyingyu.com 的 emoji 形态重描，**hand 换词为 eye**；线图 10 词 / **149 笔 / 775 B**，20 段 ADP1 **280.0 KiB**。**音标不新增、不改动，`words/` 词表零改动；设备端渲染链路（`wordline-v1`、`LV_EVENT_DRAW_MAIN`）不变。** 逐词改前/改后对比见 `/workspace/p0-words-review.html`。

---

## 0. 与 v0.3 方案稿的 3 项校订（先读这一节）

方案稿 v0.3 是在**尚未取得底层仓库源码**时基于调研推断写的。本次浅克隆 + 读取 `doc/ARCHITECTURE.md`、`XIAOZHI_BUILD.txt`、`partitions.csv` 与各 HAL 头文件后，发现 3 处必须校正。**用户已拍板的产品决策（UP/DOWN 直选、按钮缩小、单词模式菜单置顶、进/出两步、孩子不进菜单）全部保留，一个不改**；校正的只是实现落点。

| 编号 | v0.3 假设 | 真实仓库 | P0 处理 |
| --- | --- | --- | --- |
| **C1** | 长按 **DOWN** 3 秒呼出菜单；长按 **OK** = 问小智 | `XIAOZHI_BUILD.txt` 第 4 条：**「对话页长按确认打开菜单」**，菜单里 UP/DOWN 换焦点、短按 OK 执行；现有菜单共 7 项、按五行窗口滚动 | **统一复用「长按 OK 呼出菜单」**，与全机一致、零新增按键逻辑。详见 §0.1 |
| **C2** | 在 `main/` 下新增 6 个文件，不动 `components/bsp` | 真实为**端口-适配器六边形**：`components/core/{ports,services}`、`components/ui/presentation`、`components/platform/xiaozhi`、`components/app`；`main/main.c` 只有 `app_run()` | 业务状态机落 `services`、页面落 `presentation`、编排落 `app`，见 §2 |
| **C3** | 需自建 Opus 解码；未指定 Flash 落位 | 仓库**自带 `ADP1` 容器 + `adpcm_decode()` + `audio_service_play()`**（纯逻辑、无 FPU、无第三方解码器）；`audio` 分区 `0x80000` = 512 KiB | 发音包已按 ADP1 生成，直接复用现成解码器；落位见 §3.4 |

另有两条**不是冲突、只是补充**：LVGL 内存池已固定 `CONFIG_LV_MEM_SIZE_KILOBYTES=40`，架构文档记载「无 PSRAM，堆曾观测低至约 1KB」——比方案稿估算更紧，故 §3.5 重新做了内存预算；屏幕物理 240×320，仓库规格文档按「240×296 可用区 + 24px 状态栏」布局，P0 沿用该惯例。

### 0.1 C1 详解：把两个长按键合并成一个（<b>已确认定稿</b>，保留一键回退）

v0.3 里有两个长按键：`DOWN 长按 = 菜单`、`OK 长按 = 问小智`。而真实固件**已经把「长按 OK」用作全局菜单入口**。若照搬，一个键会同时承担三件事，孩子和家长都会混。

P0 建议改为：

| 动作 | v0.3 | P0 校订后 |
| --- | --- | --- |
| 呼出菜单 | DOWN 长按 3 秒 | **OK 长按**（复用现成） |
| 对话态菜单首项 | 单词模式（置顶、默认高亮） | **不变** |
| 单词模式菜单首项 | 返回小智（动态替换） | **不变**（并去掉原末尾「返回」，保持 7 项 / 五行窗口不变） |
| 问小智 | OK 长按直接切 | 长按 OK → OK（首项「返回小智」）→ 回对话页口头问；P1 再做「讲完自动回」 |
| 进 / 出单词模式 | 两步 | **仍两步、完全对称**（长按 OK + OK） |

好处：全机只有一个长按键心智；v0.3 那个「动态首项」优化恰好承接了「问小智 / 退出」语义；P0 不碰在线语音链路也能闭环。
代价：「问小智」多了一步、且不会自动回单词模式（用 NVS 记住进度，回来接着那个词）。

> **回退开关（保留，不启用）**：若你希望保留 v0.3 原样（`DOWN 长按` 呼菜单 + `OK 长按` 问小智），只需在 `app.c` 的 `on_button` 里把 `HAL_BTN_LONG` 的 `index==2` 改判 `index==1`，并给 `UI_PLAY_OK` 长按单独开分支即可，其余文件不动。该开关默认不启用；真机若发现长按 OK 不顺手再启用。

---

## 1. 目标与验收标准（acceptance）

| 项 | 要求 |
| --- | --- |
| 离线闭环 | **不连 Wi-Fi、不连 NAS、不连小智服务端**也能完整跑通 7 屏；开机进小智、进菜单、选单词模式即可玩 |
| 词量 | 内置 **10 词**（`apple/banana/cat/fish/sun/star/milk/eye/tree/car`），跨 6 单元抽样，每词一张线图 + 正常/慢速两版发音 |
| 认知负荷 | 孩子**不识字**：屏幕上不出现 `word` / `cn` / `ipa` / `say` 任何文本；只靠图案、线图、声音 |
| 按钮尺寸 | 两个图案各 **84×52**，合计约屏 **11%**（< 1/4），线图区放大到约 **160×160** |
| 正反馈 | 「认识」与「读对了」只给正反馈；「不记得 / 没读对」**刻意不设计任何失败符号**，只给「耳朵 + 循环箭头 + 慢速重播」 |
| 资源纪律 | 不新增任何全屏位图、不新增 LVGL canvas 缓冲、不引入新的解码器；新增 SRAM 峰值 **≤ 2 KiB** |
| 互斥 | 单词模式与小智对话态**并列互斥**；进入前释放语音资源，退出后恢复，互不抢占音频 |
| 可回溯 | 词序进度写入 NVS，断电/退出后回来接着当前词 |
| 稳定 | `idf.py build` 通过；仓库既有 Python 契约测试 + 全量 host 单测全绿；真机连续按 100 次不重启、不 WDT |

**P0 验收清单（可勾选）**

- [ ] 冷启动进小智对话页 → 长按 OK → 菜单第 1 项高亮「单词模式」→ OK → 进入主屏
- [ ] 主屏：线图可辨认、两个图案各 ≤ 1/4 屏、无文字
- [ ] UP 短按 → 认识反馈 → 1.5 s 自动下一词
- [ ] DOWN 短按 → 不记得反馈 → 慢速重播 → 回主屏
- [ ] OK 短按 → 重播当前词发音；长按 OK → 菜单首项「返回小智」→ OK 回对话页
- [ ] 退出再进入，停在同一词
- [ ] 全程断开路由器也能玩
- [ ] `idf.py size` 确认 app 分区余量；`heap_caps_get_free_size()` 打印进入前后基线

---

## 2. 分层落位（按真实仓库结构）

> 铁律（架构文档 §2）：**所有箭头指向 `ports`（命名空间 `hal/`）**。`services` 纯 C、不含 ESP-IDF / LVGL 头文件、可在 PC 上单测；`presentation` 是唯一（除 `lvgl_port_setup.c`）出现 `lvgl.h` 的业务组件。

```
components/
├── core/
│   ├── ports/include/hal/            # 只读：hal_button.h / hal_kv.h / hal_display.h / hal_audio.h
│   └── services/
│       ├── include/services/         # + word_mode.h        ← 新增：状态机/词序/按键语义（纯 C）
│       ├── src/                      # + word_mode.c        ← 新增：可 PC 单测
│       └── CMakeLists.txt            # 改：加入 word_mode.c
├── ui/presentation/
│   ├── include/presentation/
│   │   ├── ui_plays.h                # 改：ui_play_id_t 加 UI_PLAY_WORDMODE（首位）
│   │   └── ui_wordmode.h             # + 新增：页面接口
│   ├── src/
│   │   ├── ui_plays.c                # 改：菜单首项 + 动态首项替换
│   │   └── ui_wordmode.c             # + 新增：7 屏 LVGL 绘制 + 笔画直绘 + ADP1 流式播放
│   └── CMakeLists.txt                # 改：加入 ui_wordmode.c
├── platform/xiaozhi/                 # 不改（只调用 xiaozhi_stop/resume）
├── app/
│   ├── include/app/app_plays.h       # 改：加 3 个入口
│   ├── src/
│   │   ├── app.c                     # 改：注册 select 分支 + 菜单触发（C1）
│   │   ├── app_plays.c               # 改：模式切换 + 资源交接 + 按键转发
│   │   └── assets/                   # + 新增：p0_pack.h / p0_audio.h（从 /workspace/p0-pack/ 拷入）
│   └── test/test_wordmode_contract.py # + 新增：契约测试
└── tests/host/wordmode/              # + 新增（可选）：纯逻辑 ctest
tools/                                # + 新增：gen_lineart.py / gen_audio.py / gen_adpcm.py（留档可复现）
```

**明确不改**：`components/bsp`（本仓库不存在该目录）、`components/core/ports/`、`components/platform/xiaozhi/`、`partitions.csv`（方案 A 下）、`sdkconfig.defaults`（LVGL 40 KiB 池不动）、`main/main.c`。

---

## 3. 数据（data）

### 3.1 线图：`p0_pack.h`（已生成，775 B）

```
每词字节流： magic(2B, 'W''L') + stroke_count(1B) + n × 5B
每笔 5B   ： [opcode, x1, y1, x2, y2]   坐标域 0..200，stroke_width=3
opcode    ： 1=LINE  2=CIRCLE  3=RECT  4=DOT
```

```c
typedef struct {
    const char    *word;        /* TTS / 家长 / 小智用，不上屏 */
    const char    *say;         /* 6 岁中文讲解提示，不上屏 */
    const uint8_t *strokes;
    uint16_t       len;
    uint8_t        stroke_count;
    uint8_t        syllables;
} word_p0_t;
static const word_p0_t kWordP0[WORD_P0_COUNT];   /* WORD_P0_COUNT = 10 */
```

坐标映射（0..200 → 屏幕约 160×160 绘图区、居中 (120,120)）：
`sx = 40 + x * 0.8`，`sy = 40 + y * 0.8`（再叠加绘图区 top offset）。绘制时统一深墨色 `#202a33`、线宽 3 px。

> **绘制方式（内存关键）**：**不用 `lv_canvas`**（160×160×2 ≈ 51 KiB，会压垮 40 KiB 的 LVGL 池）。改为自定义 `lv_obj` + `LV_EVENT_DRAW_MAIN` 回调里用 `lv_draw_line()` / `lv_draw_arc()` 逐笔直绘，`lv_draw_line_dsc_t` 为栈上局部变量，**零中间帧缓冲**。曲线已用折线近似，无需贝塞尔。

### 3.2 发音：`p0_audio.h` + `audio/*.adp1`（已生成，280.0 KiB）

完全对齐仓库现成 `services/audio_clip.h`：

```c
#define AUDIO_CLIP_MAGIC 0x31504441u   /* "ADP1" */
typedef struct {
    uint32_t magic;        /* ADP1 */
    uint16_t sample_rate;  /* 16000 */
    uint8_t  channels;     /* 1 */
    uint8_t  reserved;
    uint32_t num_samples;  /* 精确样本数 */
} audio_clip_hdr_t;        /* 12 B 小端，其后紧跟 IMA-ADPCM nibble 流 */
```

`p0_audio.h` 已导出：

```c
#define WORD_AUDIO_RATE  16000
#define WORD_AUDIO_COUNT 10
typedef struct { const uint8_t *data; uint32_t len; } word_clip_t;
typedef struct { const char *word; word_clip_t normal; word_clip_t slow; } word_audio_t;
/* static const word_audio_t kWordAudio[WORD_AUDIO_COUNT]; */
```

| 项 | 值 |
| --- | --- |
| 采样率 / 声道 / 编码 | 16 kHz / mono / IMA-ADPCM（每字节 2 样本，低 nibble 在前） |
| 每词两版 | `normal` + `slow`（-30% 语速，用于「不记得 / 再听一次」） |
| 总量 | 20 段 / **286704 B ≈ 280.0 KiB**；单段 10.1–18.4 KiB |
| 语音 | `en-US-AnaNeural`（微软童声，edge-tts）；生成脚本留档可复现 |
| 校验 | 20 段全部经 `adpcm_decode()` 回环解码，MAE 17–57（16bit 域，听感无损） |

### 3.3 词表字段（与 `/workspace/words/*.json` 严格一致）

`id / word / ipa / cn / syllables / say / draw / strokes_max / unit / tags / stroke_count / binary_size_bytes / strokes`。
P0 只用 `word`（查发音 key）、`strokes`（渲染）、`syllables`（未来节奏）；`ipa / cn / say / draw` **一律不上屏**。

### 3.4 Flash 落位（两个方案）

| | 方案 A（P0 推荐） | 方案 B（余量不足时） |
| --- | --- | --- |
| 做法 | `p0_pack.h` / `p0_audio.h` 的 `static const` 编进 `.rodata`，ESP-IDF 默认 **Flash XIP 映射，零 SRAM** | 把 20 段 ADP1 打成 `words.bin`，刷入 `audio` 分区尾部或新建只读分区，`esp_partition_mmap` 零拷贝读 |
| 分区改动 | 无 | 需改 `partitions.csv` + `release.py` 六段镜像校验 |
| 代价 | 占 app 分区（`factory 0x300000` = 3 MiB）约 285 KiB | 多一个镜像段、刷机步骤变多 |
| 前提 | `idf.py size` 确认 app 余量 > 320 KiB | — |

> `audio` 分区 `0x37a000, 0x80000`（512 KiB）现有余量约 227 KiB，**装不下** 280 KiB 的完整发音包，所以方案 B 需要扩分区或压缩（降到 8 kHz 可减半）。P0 先走 A，把「换发音不重编译应用」留到 P1。

### 3.5 内存预算（按「堆曾低至 1KB」重做）

| 项 | 大小 | 存放 | 新增 SRAM |
| --- | --- | --- | --- |
| 线图数据 10 词 / 149 笔 | 775 B | `.rodata` / Flash XIP | **0** |
| ADP1 发音包 | 286704 B | `.rodata` / Flash XIP | **0** |
| LVGL 对象（7 屏，按需创建/删除） | ~3–5 KiB | LVGL 40 KiB 池（已存在） | 由既有池承载 |
| 笔画直绘 `lv_draw_*_dsc_t` | ~64 B | 栈 | ~64 B |
| ADPCM 解码块 256 B → PCM 512×int16 | 1 KiB | **静态全局缓冲**（不要放栈） | **1 KiB** |
| 状态机 + 进度 | ~80 B | 静态 | ~80 B |
| **新增 SRAM 峰值** | | | **≈ 1.2 KiB** |

**交接纪律（比数字更重要）**：进入单词模式前调 `xiaozhi_stop()`，等 `xiaozhi_stopped()` 为真（该版本结束语音后会释放编解码器、麦克风、语音套接字与 TLS 客户端，仅保留 Wi-Fi 与形象），**腾出的堆远大于新增的 1.2 KiB**；退出时 `xiaozhi_resume()`。两种模式绝不共存。

---

## 4. 页面与线稿（pages · 7 屏，菜单含 2 态，240×320）

> 共 **7 个页面、8 张线稿**：「②′」是「②」在单词模式内的动态首项状态，不额外算一屏。
> 沿用仓库「深蓝黑 / 暖杏焦点、圆角外黑」的小智视觉语言；单词模式内部改用**纸白底 `#fbfaf6` + 深墨 `#202a33`**，与对话态形成「切换到学习」的直觉区分。每屏切换只 `lv_obj_delete_async(old)` + 建新对象，不做整页动画。

| # | 屏 | 元素 | 上屏文字 | 按键 | 跳转 |
| --- | --- | --- | --- | --- | --- |
| ① | **日常小智界面** | 原固件原样：深色圆角、动态形象、声音驱动嘴型、CW2017 电量条 | 原样 | OK 短按=开始/结束对话；**OK 长按=呼出菜单** | →② |
| ② | **右侧菜单** | 7 项五行窗口滚动；**第 1 项「单词模式」置顶且默认高亮**（暖杏）；对话态下末项为「返回」 | 有（给家长） | UP/DOWN 换焦点；OK 短按执行 | →③ |
| ②′ | **右侧菜单（单词模式内）** | 同上，但**首项动态换成「返回小智」**，并去掉原末项「返回」，保持 7 项 | 有 | 同上 | →① |
| ③ | **看图 + 二选一（主屏）** | 顶部细状态条；中部 160×160 线图直绘；底部两个 84×52 图案（左=拇指「认识」、右=问号「不记得」） | **无** | UP=认识 / DOWN=不记得 / OK 短按=再听 | →④⑤ |
| ④ | **跟读**（P0 占位） | 一个「麦克风/小嘴」图案 + 顶部细条；P0 不采音，仅提示可以跟着念 | 无 | OK 短按=跳过 | →⑤⑥ |
| ⑤ | **读对了 / 认识** | 大拇哥 + 星星，暖杏高亮；**1.5 s 自动进下一词** | 无 | 任意键=立即跳过 | →③ |
| ⑥ | **没读对 / 不记得** | 耳朵 + 循环箭头 + 喇叭；自动播放 `*_slow.adp1` 后回主屏 | 无 | OK 短按=再慢速听一次 | →③ |
| ⑦ | **问小智（P0 = 返回对话态）** | 切回 ① 界面；顶部保留一个单词缩略图给家长看（可选） | 原样 | 同 ① | 经 ②′ 回 ③，恢复 NVS 进度 |

> v0.3 的 ⑦ 是「小智讲完自动回单词模式」，依赖在线语音链路与上下文注入，属 **P1/P2**。P0 降级为「回对话页口头问」，并用 `hal_kv` 记 `wordmode:index`，回来接着当前词。

---

## 5. 按键与导航（controls）

真实 HAL（`components/core/ports/include/hal/hal_button.h`）：

```c
typedef enum { HAL_BTN_CLICK, HAL_BTN_DOUBLE, HAL_BTN_LONG,
               HAL_BTN_PRESS, HAL_BTN_RELEASE } hal_btn_event_t;
typedef void (*hal_btn_cb_t)(int index, hal_btn_event_t e, void *user);
/* index: 0=UP  1=DOWN  2=OK（与 platform_lvgl_attach_buttons 的 prev/next/enter 一致） */
```

| 模式 | 按键 | 语义 |
| --- | --- | --- |
| 对话态 ① | OK 短按 | 开始 / 结束对话（原固件） |
| 对话态 ① | **OK 长按** | **呼出菜单 ②**（复用现成，C1） |
| 菜单 ②/②′ | UP / DOWN 短按 | 上 / 下移焦点 |
| 菜单 ②/②′ | OK 短按 | 执行当前项 |
| 单词模式 ③④⑤⑥ | **UP 短按** | **认识（拇指）** |
| 单词模式 ③④⑤⑥ | **DOWN 短按** | **不记得（问号）** |
| 单词模式 ③④⑤⑥ | OK 短按 | 再听 / 结束跟读 / 跳过结果 |
| 单词模式 ③④⑤⑥ | **OK 长按** | **呼出菜单 ②′ → 返回小智** |

- `HAL_BTN_PRESS` 是「按下即触发、低延迟」，`HAL_BTN_CLICK` 有约 180 ms 去双击窗口。图案反馈**用 `HAL_BTN_PRESS` 求跟手**，菜单执行**用 `HAL_BTN_CLICK` 避免误触**。
- 长按后的松手**不得再触发第二次操作**（`XIAOZHI_BUILD.txt` 第 6 条既有约束，直接沿用 `app_plays_button()` 的裸手势转发语义）。
- 孩子平时只碰短按；**永远不进菜单、不选单元、不看文字**。

---

## 6. 状态机

**顶层（app 层持有）**

```
        ┌──────────────── OK 长按 → 菜单② → OK(首项「单词模式」) ──────────────┐
        │                                                                      ▼
  [XIAOZHI] ── xiaozhi_stop() → 等 xiaozhi_stopped() ──▶ [WORDMODE] ── ui_wordmode_open()
        ▲                                                                      │
        └──── xiaozhi_resume() ◀── OK 长按 → 菜单②′ → OK(首项「返回小智」) ─────┘
```

互斥不变量：任一时刻只有一个模式持有 `hal_audio_t`；切换必须等上一方 `*_stopped()` / `*_close()` 完成。

**单词模式内部（services/word_mode.c，纯 C）**

```
IDLE ──open──▶ SHOW(③) ──UP──▶ KNOWN(⑤) ──1.5s/任意键──▶ NEXT ──▶ SHOW
                    │
                    ├──DOWN──▶ UNKNOWN(⑥) ──慢速播完/OK──▶ SHOW
                    ├──OK短按─▶ REPLAY(播 normal) ──▶ SHOW
                    └──OK长按─▶ MENU_REQUEST(交给 app) ──▶ (退出)
SHOW 每进入一次：word_index = (word_index + 1) % 10，并 hal_kv_set("wordmode:index", ...)
```

`word_mode.c` 不 include 任何 LVGL / IDF 头，只输出 `word_action_t` 枚举给上层，便于 PC 单测覆盖全部转移。

---

## 7. 接口改动（真实签名）

### 新增 `components/core/services/include/services/word_mode.h`

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { WM_ACT_NONE, WM_ACT_SHOW, WM_ACT_REPLAY, WM_ACT_SLOW,
               WM_ACT_KNOWN, WM_ACT_UNKNOWN, WM_ACT_NEXT,
               WM_ACT_MENU_REQUEST } word_action_t;
typedef enum { WM_SCREEN_SHOW, WM_SCREEN_LISTEN, WM_SCREEN_KNOWN,
               WM_SCREEN_UNKNOWN, WM_SCREEN_MENU } wm_screen_t;

void     word_mode_init(int word_count, int saved_index);
void     word_mode_key(int index, bool long_press);   /* 0=UP 1=DOWN 2=OK */
void     word_mode_tick(uint32_t now_ms);             /* 驱动 1.5s 自动跳转 */
word_action_t word_mode_poll(void);                   /* 取出并清空待执行动作 */
int      word_mode_index(void);
wm_screen_t word_mode_screen(void);
```

### 新增 `components/ui/presentation/include/presentation/ui_wordmode.h`

```c
#pragma once
#include "lvgl.h"
#include "services/word_mode.h"

typedef void (*ui_wordmode_menu_cb_t)(void *user);   /* 长按 OK：请求 app 呼出菜单 */
void ui_wordmode_open(ui_wordmode_menu_cb_t on_menu, void *user);
void ui_wordmode_render(wm_screen_t screen, int word_index);
void ui_wordmode_key(ui_play_key_t key, hal_btn_event_t event); /* 需 LVGL 锁 */
void ui_wordmode_close(void);
bool ui_wordmode_active(void);
/* 发音：从 p0_audio.h 取 ADP1 段，流式解码后送 audio_service_play() */
int  ui_wordmode_play(const char *word, bool slow);
```

### 改 `components/ui/presentation/include/presentation/ui_plays.h`

```c
typedef enum { UI_PLAY_WORDMODE,        /* ← 置顶，保证菜单第 1 项 */
               UI_PLAY_ANSWER, UI_PLAY_CLICKER, UI_PLAY_DODGE,
               UI_PLAY_XIAOZHI, UI_PLAY_CONNECT } ui_play_id_t;
/* 新增：单词模式内把首项换成「返回小智」并隐藏原末项「返回」 */
void ui_plays_set_wordmode_context(bool in_wordmode);
```

### 改 `components/app/include/app/app_plays.h`

```c
bool app_plays_open_wordmode(int best);      /* 与 app_plays_open_xiaozhi 同锁契约 */
bool app_plays_wordmode_active(void);
void app_plays_wordmode_button(int index, hal_btn_event_t event);
```

### 复用的现成接口（不改）

```c
/* 音频：components/core/services/include/services/{adpcm.h,audio_clip.h,audio_service.h} */
void adpcm_state_reset(adpcm_state_t *st);
int  adpcm_decode(adpcm_state_t *st, const uint8_t *in, int nbytes, int16_t *out);
int  audio_service_configure(audio_service_t *, uint32_t hz, uint8_t bits, uint8_t ch);
int  audio_service_play(audio_service_t *, const void *pcm, size_t bytes);

/* 存储：components/core/ports/include/hal/hal_kv.h */
int  hal_kv_get(hal_kv_t *, const char *key, void *buf, int cap, int *out_len);
int  hal_kv_set(hal_kv_t *, const char *key, const void *data, int len);

/* 小智：components/platform/xiaozhi/include/xiaozhi.h */
void xiaozhi_stop(void);  bool xiaozhi_stopped(void);  void xiaozhi_resume(void);
```

---

## 8. 音频播放链路（复用现成 ADP1，零新解码器）

```
kWordAudio[i].normal/slow.data  (Flash XIP, 12B ADP1 头 + nibble 流)
   │  校验 magic == 0x31504441、sample_rate == 16000、channels == 1
   ▼
audio_service_configure(&svc, 16000, 16, 1)
   ▼  分块循环（每块 256 B，绝不整段解码）
adpcm_state_reset(&st) 一次 → adpcm_decode(&st, in+12+off, 256, pcm_buf[512])
   │  → 512 个 int16 = 1024 B PCM
   ▼
audio_service_play(&svc, pcm_buf, 1024)
   ▼  循环到 num_samples 用尽；播完置 idle，触发 ⑥ → ③ 回主屏
```

- `pcm_buf` 用 **1 KiB 静态全局**，不要放栈（C3 栈紧）。
- 慢速版不做实时变速（无 FPU），直接用预生成的 `*_slow.adp1` 段，切换零成本。
- 播放期间 `ui_wordmode_key()` 收到 OK 短按应立即中止当前块循环并重播；不阻塞 LVGL 任务（若 `audio_service_play` 阻塞，放到 plays worker，参照 `app_plays_config_t.acquire_audio` 的锁契约）。

---

## 9. 明确不做（YAGNI / 分阶段边界）

| 不做 | 归属 | 原因 |
| --- | --- | --- |
| 跟读评测、ASR 字幕比对、童音宽容表 | **P2** | 依赖在线服务 `stt.text`，P0 纯离线 |
| 「讲完自动回单词模式」、上下文注入 `listen.detect.text` | **P1/P2** | 依赖在线小智服务端，需实机验证 |
| NAS 下发词库 / 家长网页换词表 | **P1** | 路径已备好 `/vol1/1000/vvvv/words`，但 P0 不联网 |
| wordmemory 的 SM-2 复习曲线、登录、PWA、七牛云 | 不引入 | 只取 `word` / `progress` 数据形状，将来再说 |
| 设备端选单元、显示单词文本 / 中文 / 音标 | 不做 | 孩子不识字；单元切换只放家长端 |
| `lv_canvas` / 全屏位图 / 阴影发光 / 整页切换动画 | 不做 | 40 KiB LVGL 池 + 堆 1KB，会 OOM |
| 新增解码器、改 `partitions.csv`、改 `sdkconfig.defaults` | 不做 | 方案 A 下均不需要 |

---

## 10. 实现步骤清单（有序，逐步可验证）

1. **拷资产**：`/workspace/p0-pack/p0_pack.h` + `p0_audio.h` → `components/app/src/assets/`；3 个 `gen_*.py` → `tools/`。
2. **纯逻辑层**：写 `services/word_mode.c/.h`，加进 `services/CMakeLists.txt`。PC 上 `gcc` 直接编译 + 单测覆盖 §6 全部转移（不依赖任何 IDF 头）。
3. **表现层骨架**：写 `ui_wordmode.c/.h`，加进 `presentation/CMakeLists.txt`。先只做 ③ 主屏 + 笔画直绘（`LV_EVENT_DRAW_MAIN`），**不接音频**，`idf.py build` 通过。
4. **发音接入**：实现 `ui_wordmode_play()` 的 ADP1 分块解码；先用 `apple` 一段验证 `adpcm_decode` 输出有声音，再放开 20 段。
5. **补齐 7 屏**：④⑤⑥ 反馈屏 + 1.5 s 定时跳转（`word_mode_tick`）。
6. **菜单集成**：`ui_plays.h` 枚举置顶 `UI_PLAY_WORDMODE`；`ui_plays.c` 首项默认高亮 + `ui_plays_set_wordmode_context()` 动态替换；`app.c` 的 select 回调新增分支。
7. **资源交接**：`app_plays.c` 实现 `app_plays_open_wordmode()` —— `xiaozhi_stop()` → 等 `xiaozhi_stopped()` → `ui_wordmode_open()`；退出反向 `xiaozhi_resume()`。打印进入前后堆基线。
8. **按键落地（C1）**：`app.c` 长按 OK 触发 `ui_plays_menu()`；`app_plays_wordmode_button()` 转发裸手势。实机核对触发键究竟是长按 OK 还是短按 OK（以 `app.c` 现有 `on_button` 为准）。
9. **进度持久化**：`hal_kv` 存 / 取 `wordmode:index`。
10. **契约测试**：写 `test_wordmode_contract.py`（见 §11）；跑仓库全量测试命令。
11. **真机验收**：按 §1 清单逐项过；重点让 6 岁小朋友实按，确认缩小后的 84×52 两个图案仍认得出。
12. **出包**：`idf.py build` → `python scripts/release.py` → `python tools/verify_release.py`。

---

## 11. 测试与验收

### 11.1 新增 `components/app/test/test_wordmode_contract.py`（照仓库既有契约测试写法）

断言要点：
- `ui_plays.h` 中 `UI_PLAY_WORDMODE` 位于枚举**首位**；`ui_plays.c` 含 `ui_plays_set_wordmode_context`。
- `ui_wordmode.c` **不含** `lv_canvas_create` / `lv_image_create` / `lv_obj_set_style_shadow` / 全屏 `lv_img_dsc_t`；含 `LV_EVENT_DRAW_MAIN`。
- `word_mode.c` **不含** `lvgl.h` / `esp_` / `driver/` 头（保证可 PC 单测）。
- `p0_pack.h` 的 `WORD_P0_COUNT` 与 `p0_words.json` 的 `totals.words` 一致；每词 `stroke_count` 与字节流长度自洽。
- `p0_audio.h` 的 `WORD_AUDIO_COUNT` 与 `audio/manifest.json` 词数一致，且每段头 4 字节为 `41 44 50 31`。
- 按键映射中**不存在**「长按 DOWN 呼出菜单」（C1 校订生效）；若回退 v0.3，改这条断言即可。

### 11.2 全量回归（`XIAOZHI_BUILD.txt` 给定的命令，直接复用）

```bash
python -m unittest discover -s tests/host -p 'test_*.py'
python -m unittest discover -s components/app/test -p 'test_*.py'
python -m unittest discover -s components/ui/presentation/test -p 'test_*.py'
python -m unittest discover -s tools -p 'test_*.py'
python tools/gen_ui_fonts.py --check && python tools/gen_cn_font.py --check
python tools/gen_font_oracle.py --check && python tools/gen_font_notices.py --check
cmake -S tests/host/ui -B build-native-ui -DPET_ART_REFERENCE_PYTHON=<含Pillow的Python> && cmake --build build-native-ui && ctest --test-dir build-native-ui --output-on-failure
```

### 11.3 整机与真机

- `idf.py build` 通过；`idf.py size` 确认 app 分区余量 > 320 KiB；`CONFIG_LV_MEM_SIZE_KILOBYTES` 仍为 40。
- 真机：断网可玩、7 屏可达、三键无歧义、长按松手不二次触发、连续 100 次按键无重启 / 无 `task_wdt`、退出回小智对话正常。
- **产品验收（最重要）**：让小朋友实按，确认「UP=拇指、DOWN=问号」零门槛。若不成立，P0 的价值就在这一步——改图案或改映射，不牵动架构。

---

## 12. 风险与回退

| 风险 | 触发条件 | 回退 |
| --- | --- | --- |
| **C1 按键映射** | 你或真机觉得长按 OK 不顺手 | 改 `app.c` 一处判断即回退 v0.3（`DOWN 长按` 呼菜单 + `OK 长按` 问小智），其余文件不动 |
| app 分区余量不足 | `idf.py size` 显示 < 320 KiB | 转方案 B：ADP1 单独成段刷 `audio` 分区；或降采样到 8 kHz 减半 |
| `audio_service_play` 阻塞 LVGL | 播放时掉帧 / WDT | 移到 plays worker，按 `app_plays_config_t.acquire_audio` 锁契约执行 |
| 长按 OK 实际是短按 OK | 实机核对 `app.c` 发现与文档不符 | 以实现为准，同步改 §5 / §10 与契约断言（**不影响架构**） |
| 40 KiB LVGL 池不够 7 屏对象 | 创建对象返回 NULL | 每屏用完 `lv_obj_delete_async`，不常驻；必要时把 ⑤⑥ 合并为同一屏换图标 |
| `stt.text` 拿不到 | P2 才涉及 | 已在 v0.3 第 12 节固化：退化为 `llm.emotion` 驱动结果屏，隔离在 `word_proto.c` |

---

## 附录 A：已生成资产清单与校验结果

| 文件 | 内容 | 大小 |
| --- | --- | --- |
| `p0-pack/p0_pack.h` | 10 词线图 C 数组（`static const`，Flash XIP） | 6.9 KiB |
| `p0-pack/p0_pack.bin` | 同上二进制（方案 B 用） | 819 B |
| `p0-pack/p0_words.json` | 词包数据源（含 strokes / 字段说明） | 18 KiB |
| `p0-pack/p0_audio.h` | 20 段 ADP1 的 C 数组 | 1.8 MiB 文本 / 280 KiB 二进制 |
| `p0-pack/audio/*.adp1` | 20 段设备可直接解码的发音 | 280.0 KiB |
| `p0-pack/audio/*.opus` | 中间产物（Ogg Opus，设备不解，**仅留档**） | 41.6 KiB |
| `p0-pack/audio/manifest.json` | 每段字节数 / 样本数 / 解码 MAE | 7 KiB |
| `p0-pack/gen_lineart.py` / `gen_audio.py` / `gen_adpcm.py` | 可复现生成脚本 | — |
| `words/`（6 单元 60 词） | **P1** NAS 下发用，路径 `/vol1/1000/vvvv/words` | — |

校验：`p0_words.json` 10 词 / 149 笔 / 775 B；20 段 ADP1 全部 `adpcm_decode()` 回环通过；6 单元 60 词 JSON 全部有效。

## 附录 B：构建与烧录

```bash
# 环境：ESP-IDF v5.5.3，依赖版本见 dependencies.lock
. $IDF_PATH/export.sh
idf.py build
python scripts/release.py
python tools/verify_release.py releases/<生成的ZIP> --build-dir build
```

> 该版本**六段镜像**（`XIAOZHI_BUILD.txt` 第 19 条明确：升级必须配套刷入 `petart.bin` / `companion.bin` 与应用，不能只更新应用 BIN）。P0 方案 A 不新增镜像段，仍按现有 release 流程走。`.18` 小程序合并版使用 `partitions-miniapp.csv`，**两种布局不可混刷**——请确认手上的设备刷的是标准 `partitions.csv` 版。

---

*本 PRD 与 `wordmode-plan.html`（v0.3.1）配套。校订 C1 已确认定稿；其余按本文件直接投喂实现。*
