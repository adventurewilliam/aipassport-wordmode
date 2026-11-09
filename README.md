# AI Passport 小朋友单词模式 · P0

> 公开仓库：[`adventurewilliam/proxyhm`](https://github.com/adventurewilliam/proxyhm) · 分支 `main`（本仓库为公开仓库，代码与思路说明均可直接查看）
> 原仓库空壳说明保留在 [`docs/upstream-proxyhm-README.md`](docs/upstream-proxyhm-README.md)。

给 **6 岁、不识字**的小朋友用的英文单词固件：**看图 → 听怎么读 → 认识/不记得二选一 → 不懂就问小智**。

- 底层：[`arraylee/ai-passport-xiaozhi-custom`](https://github.com/arraylee/ai-passport-xiaozhi-custom)（小智固件移植到 AI Passport）
- 硬件：`aipassport.dsh` = ESP32-C3 / 8 MB Flash / **无 PSRAM** / 240×320 竖屏 / **无触屏，只有 UP·DOWN·OK 三键** / ES8311 麦克风+喇叭
- 词库内核参考：[`doramart/wordmemory`](https://github.com/doramart/wordmemory)；图案参照：[bbyingyu.com](https://www.bbyingyu.com/)
- 语音：**在线小智服务器**（不自建）；数据将来放**飞牛 NAS** `/vol1/1000/vvvv/words`（P1）

> 本仓库是**独立的设计 + 新增代码仓库**，不含上游固件源码。集成方法见 [`integration/INTEGRATION.md`](integration/INTEGRATION.md)。

---

## 一、思路：为什么长这样

整个设计被三条硬约束逼出来，其余都是推论。

### 约束 1：只有三键、没有触屏，孩子不识字

孩子**没法「点拇指图案」**，也不该要求他「先移焦点再确认」。所以把每个键直接绑成一个意思，一次按下即表达：

| 键 | 意思 | 图案 |
| --- | --- | --- |
| **UP 短按** | 认识 / 会读了 | 👍 拇指 |
| **DOWN 短按** | 不记得 | ❓ 问号 |
| **OK 短按** | 再听一次 | 🔁 |
| **OK 长按** | 呼出菜单（给家长） | — |

两个图案压到 **84×52**，合计约屏 11%（<1/4，按用户要求缩小），腾出的空间给线图放大到 160×160。屏幕上**不显示任何单词、音标、中文**——孩子不识字；`word`/`cn`/`say` 只给 TTS、家长和小智用。
「不记得 / 没读对」**刻意不设计任何失败符号**，只给「耳朵 + 循环箭头 + 慢速重播」，避免挫败感。

### 约束 2：一张全屏位图 ≈ 150 KiB，而 C3 全部 SRAM 才约 400 KiB

用户原本想「让 AI 生成简单线图」。方向对，但**不能生成图片**：160×160 RGB565 就 50 KiB，而 LVGL 内存池固定 40 KiB、架构文档记载「堆曾低至约 1 KB」。

所以让 AI/备课侧输出**笔画指令**而不是图片：

```
每笔 5 字节：[opcode, a, b, c, d]   opcode 1=LINE 2=CIRCLE 3=RECT 4=DOT
一个词      = "WL" + 笔数(1B) + n×5B  →  约 100~400 B
```

设备端在 `LV_EVENT_DRAW_MAIN` 里**逐笔直绘**（`lv_draw_line`），禁用 `lv_canvas` / 全屏位图，零中间帧缓冲。曲线在备课侧已扁平成折线，设备只做四则运算。这与底层固件「字库映射 Flash、不占 RAM」的思路一脉相承。

P0 全部 **10 词 / 149 笔 / 775 B**，`static const` 编进 `.rodata` 走 Flash XIP，**零 SRAM**。

### 约束 3：在线小智服务器不下发结构化 custom 消息

这决定了「跟读评测」的做法。官方 `xiaozhi.me` 不会下发 `type:custom`，但会把 ASR 转写通过 `type:stt` 回传显示字幕——所以设备拿这段文字**自己做比对**即可（归一化 + 编辑距离 + 童音宽容表），零 token、零云端延迟。这属于 **P2**；P0 纯离线不做评测。
「问小智」在 P0 降级为「回对话页口头问」，用 NVS 记住词序，回来接着当前词；「讲完自动回」留到 P1/P2。

### 由三条约束推出的分层

```
设备端只做薄壳（新增 8 个文件，不动 ports / xiaozhi / 分区表）
   ├── services  纯 C 状态机 + 笔画解释器   ← 可在 PC 上 gcc 真编译单测
   ├── presentation  7 屏 LVGL + 逐笔直绘 + ADP1 分块解码
   └── app  编排：动作→副作用、资源交接、NVS 进度
重活在外面：NAS 词库与复习曲线（P1）· 在线小智语音 · 备课侧批量生成线图/发音
```

**两模式并列互斥**：进单词模式先 `xiaozhi_stop()` 等 `xiaozhi_stopped()`（该版本会释放编解码器/麦克风/套接字/TLS），退出 `xiaozhi_resume()`。靠资源交接腾堆，新增 SRAM 峰值约 **1.2 KiB**（1 KiB PCM 缓冲 + 少量状态）。

发音复用固件现成的 **`ADP1` 容器 + `adpcm_decode()` + `audio_service_play()`**，不引入新解码器：10 词 × 正常/慢速两版 = 20 段 / 280 KiB，每块 256 B 流式解码，1 KiB 静态 PCM 缓冲。

---

## 二、目录结构

```
aipassport-wordmode/
├── README.md                     本文件：思路说明 + 状态
├── integration/INTEGRATION.md    叠加到上游固件的完整步骤
├── docs/                         设计过程留档（可直接浏览器打开）
│   ├── wordmode-plan.html        框架与线稿 v0.3.1（7 屏 + 状态机 + 决策记录）
│   ├── p0-prd.md                 工程 PRD（投喂 AI 编程工具的 Markdown 版）
│   ├── p0-prd.html              PRD 可视化评审版
│   ├── p0-words-review.html     10 词图案过稿页（改前/改后 A·B 对比）
│   └── word-source-review.html  bbyingyu.com 接入评估页
├── firmware/
│   ├── components/core/services/{include/services,src}/   word_mode.* wordline.*
│   ├── components/ui/presentation/{include/presentation,src}/  ui_wordmode.*
│   ├── components/app/{include/app,src,src/assets,test}/
│   │        wordmode_app.*  p0_pack.h  p0_audio.h  test_wordmode_contract.py
│   └── patches/              对上游 6 个现有文件的改动说明 + 胶水代码
├── host/                      PC 侧验证（无需硬件）
│   ├── test_word_mode.c       状态机单测 47 用例
│   ├── test_wordline.c        笔画解释器单测 95 用例
│   ├── stubs/                 LVGL/IDF 类型桩（仅用于 -fsyntax-only）
│   ├── Makefile  verify.sh    一键跑全部 PC 检查
├── assets/                    可直接使用的成品数据
│   ├── words/                 6 单元 60 词（P1 拷到 NAS）
│   ├── audio/                 20 段 .adp1 + .opus + manifest.json
│   ├── lineart/               图案过稿 PNG
│   └── p0_words.json  p0_pack.bin  lineart-before.json  source-*.json
└── tools/                     可复现的备课脚本
    gen_lineart.py  gen_audio.py  gen_adpcm.py  render_lineart.py
```

> `docs/` 内的 HTML 是设计过程留档，其中出现的 `/workspace/...` 为生成时的沙箱路径，仅作说明用；仓库内实际路径以本文件目录表为准。

---

## 三、决策记录（都已定稿）

| # | 决策 | 结论 |
| --- | --- | --- |
| 1 | 按键映射 | **UP/DOWN 直选**（不引入焦点概念）；按钮缩小到 84×52 |
| 2 | 词表存放 | **NAS 按需下发**（换词不刷机）+ 内置 10 词保底包（断网可玩） |
| 3 | 小智服务端 | **用在线服务器**，不自建；评测走 `stt.text` 本地比对（P2） |
| 4 | 词表 | 生成适合 6 岁的 6 单元 60 词 |
| 5 | `aipassport.dsh` | 运行时不用，仅开发/烧录 |
| C1 | 菜单呼出 | 读源码后校订为**长按 OK**（`XIAOZHI_BUILD.txt` 第 4 条），合并掉两个长按键；回退开关保留但不启用 |
| — | 菜单顺序 | 单词模式置**第一项且默认高亮**，长按 OK + OK 两步进入；模式内首项动态换成「返回小智」 |
| — | 图案方案 A | 借 bbyingyu.com 的 emoji **当描图参照**，不搬彩图；重描 banana/milk/tree、hand 换 **eye** |
| — | 音标 | **不做**（小朋友看不懂）；`words/` 词表零改动、不新增字段 |

三条与早期假设不符、读源码后校正的事实：真实架构是**组件化六边形**（`core/services` → `ui/presentation` → `app`，不是 `main/` 堆文件）；菜单是**长按 OK**；发音**自带 ADP1 解码器**，不必自建 Opus。

---

## 四、当前状态（截至 2026-11-09）

### ✅ 已完成

**代码**（`firmware/components/`，8 个新增文件 + 4 份改动补丁）

| 文件 | 作用 |
| --- | --- |
| `services/word_mode.h/.c` | 纯 C 状态机：SHOW→KNOWN/UNKNOWN/REPLAY/NEXT，动作队列、长按、uint32 定时回绕安全 |
| `services/wordline.h/.c` | `wordline-v1` 笔画解释器：4 种 opcode、0..200 坐标映射、坏数据一笔不画 |
| `presentation/ui_wordmode.h/.c` | 7 屏 LVGL：`LV_EVENT_DRAW_MAIN` 逐笔直绘、进度点、两个图案、ADP1 分块非阻塞解码 |
| `app/wordmode_app.h/.c` | 编排：动作→渲染/发音/NVS/菜单，`stop→等 stopped→open` 资源交接 |
| `app/test/test_wordmode_contract.py` | 11 项契约测试（禁 canvas、枚举置顶、资产自洽、C1 校订） |
| `patches/*` | 上游 6 个文件的最小改动（2 个真 diff、1 份 CMake、1 份胶水代码） |

**数据资产**（`firmware/components/app/src/assets/` + `assets/`）：10 词线图 149 笔 / 775 B、20 段 ADP1 发音 280 KiB、6 单元 60 词词表、可复现生成脚本。

**验证结果**（`cd host && sh verify.sh` 可复现）

- 纯逻辑单测：**142 用例 / 0 失败**（`-Wall -Wextra -Werror` 零告警真编译）
- 契约测试：**11 项全过**
- 4 个固件 `.c`：`gcc -fsyntax-only` 语法与接口检查通过（LVGL/IDF 用 `host/stubs/` 类型桩）
- 资产一致性：149 笔全部 5 B / opcode 合法 / 长度自洽；20 段 ADP1 头合法且 `adpcm_decode()` 回环通过；三份数据头词名一致

### ⚠️ 未验证（沙箱无 ESP-IDF 5.5.3 / LVGL / 真机）

`idf.py build` 完整链接、LVGL 实际绘制效果、ADP1 在 ES8311 上的听感、真机堆占用、六段镜像出包。
这些必须按 [`INTEGRATION.md`](integration/INTEGRATION.md) 第 4/5 节在本地复核。**本仓库交付的是「设计定稿 + 代码骨架 + 数据资产就绪」，不是可直接烧录的固件 BIN。**

### 分阶段路线

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| **P0** | 纯离线、10 词、7 屏、三键定型；先让 6 岁小朋友实按验证「三键两图案」零门槛 | **代码完成，待真机** |
| P1 | 接飞牛 NAS 下发词库、家长网页换词表、讲完自动回 | 未开始 |
| P2 | 接在线语音链路做跟读评测（`stt.text` 本地比对 + 童音宽容） | 未开始 |

---

## 五、快速验证

```bash
cd host && sh verify.sh
# 1/3 纯逻辑单测  2/3 契约测试  3/3 四个 .c 语法检查
```

真机集成：`cat integration/INTEGRATION.md`。

---

## 许可与来源

- 本仓库的新增代码与文档为原创设计；上游固件 `arraylee/ai-passport-xiaozhi-custom` 遵循其自身许可，本仓库不包含其源码。
- 预置发音由 `edge-tts`（微软 `en-US-AnaNeural`）生成，词表参考 `doramart/wordmemory` 的数据形状、图案参照 `bbyingyu.com` 的 emoji 形态，仅作学习用途。
- `assets/source-bbyingyu.json` / `word-source-merge.json` 为抓取留档，**仅作描图参照，未并入词表**。
