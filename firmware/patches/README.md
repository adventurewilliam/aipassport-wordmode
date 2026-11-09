# 对底层固件的改动补丁

> 本仓库**只放新增代码与资产**，不包含 `arraylee/ai-passport-xiaozhi-custom` 的源码。
> 下面是把单词模式叠加到该固件时，需要改动的 6 个现有文件。
>
> - `01-ui_plays.h.diff` / `02-app_plays.h.diff` / `03-CMakeLists.diff`：头文件与构建清单，改动点明确，用标准 diff 表示。
> - `04-app-glue.c`：`app.c` / `app_plays.c` / `ui_plays.c` 三个 `.c` 的胶水实现。由于这三个文件体量大、且随上游版本演进，**不提供可一键 `git apply` 的行号 diff**，而是给出「可直接粘贴的函数 + 明确的调用位置说明」，集成时按真实源码上下文落位。
>
> 完整步骤、锁契约与验收见 [`../../integration/INTEGRATION.md`](../../integration/INTEGRATION.md)。

## 改动总览

| # | 文件 | 改动 | 性质 |
| --- | --- | --- | --- |
| 1 | `components/ui/presentation/include/presentation/ui_plays.h` | 枚举首位加 `UI_PLAY_WORDMODE`；声明 `ui_plays_set_wordmode_context()` | 2 行 |
| 2 | `components/ui/presentation/src/ui_plays.c` | 菜单首项默认高亮；单词模式内首项动态换成「返回小智」 | 胶水 |
| 3 | `components/app/include/app/app_plays.h` | 加 3 个入口声明 | 3 行 |
| 4 | `components/app/src/app_plays.c` | 模式切换 + 裸手势转发 | 胶水 |
| 5 | `components/app/src/app.c` | 注册依赖、select 分支、长按 OK 呼菜单 | 胶水 |
| 6 | 2 个 `CMakeLists.txt` | 挂进 3 个新增 `.c`，并把 assets 目录加入 include 路径 | 2 处 |

## 新增文件（无需改动，直接拷入固件树）

```
components/core/services/include/services/word_mode.h      → 同路径
components/core/services/include/services/wordline.h       → 同路径
components/core/services/src/word_mode.c                   → 同路径
components/core/services/src/wordline.c                    → 同路径
components/ui/presentation/include/presentation/ui_wordmode.h → 同路径
components/ui/presentation/src/ui_wordmode.c               → 同路径
components/app/include/app/wordmode_app.h                  → 同路径
components/app/src/wordmode_app.c                          → 同路径
components/app/src/assets/p0_pack.h                        → 同路径
components/app/src/assets/p0_audio.h                       → 同路径
components/app/test/test_wordmode_contract.py              → 同路径
```

## 三条硬约束（改动时必须遵守）

1. **`word_mode.c` / `wordline.c` 不得 include 任何 LVGL / ESP-IDF / driver 头**——它们是可在 PC 上真编译单测的纯逻辑层，契约测试会静态检查。
2. **`ui_wordmode.c` 不得出现 `lv_canvas` / `lv_image` / `shadow` / 全屏位图**——C3 无 PSRAM、LVGL 池 40 KiB，一张 160×160 RGB565 就 50 KiB。
3. **两模式绝不共存**：进入先 `xiaozhi_stop()` 并等 `xiaozhi_stopped()` 释放完编解码器/麦克风/套接字/TLS，再 `ui_wordmode_open()`；退出反向 `xiaozhi_resume()`。
