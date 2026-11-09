# 叠加到底层固件的集成指南

把本仓库的单词模式叠加到 [`arraylee/ai-passport-xiaozhi-custom`](https://github.com/arraylee/ai-passport-xiaozhi-custom)（v2.1.0-xiaozhi.18 / ESP-IDF 5.5.3）。

> **本仓库不含上游源码**，只放新增代码、资产、设计文档与改动补丁。
> 底层仓库是别人的项目，请自行 clone 后在本地合并，不要把它整包复制进本仓库。

---

## 0. 先读这两条约束

1. **六段镜像**：`XIAOZHI_BUILD.txt` 第 19 条明确——升级必须配套刷入 `petart.bin` / `companion.bin` 和应用，不能只更新应用 BIN。
2. **两种分区布局不可混刷**：`.18` 小程序合并版用 `partitions-miniapp.csv`，标准版用 `partitions.csv`。烧录前先确认手上设备刷的是哪一种。本 P0 走方案 A（`static const` 编进 `.rodata`），**不改分区表**。

---

## 1. 前置：跑一遍 PC 侧验证（1 分钟，无需硬件）

```bash
cd host && sh verify.sh
```

预期：2 个纯逻辑单测（47 + 95 = 142 用例）、11 项契约测试、4 个 `.c` 语法检查全绿。
这一步不需要 ESP-IDF / LVGL / 真机，用的是 `host/stubs/` 里的类型桩。

---

## 2. 拷贝新增文件（一一对应，不改路径）

设 `$FW` 为上游固件根目录。

```bash
FW=/path/to/ai-passport-xiaozhi-custom
R=/path/to/aipassport-wordmode

# 纯逻辑层（services，纯 C、可 PC 单测）
mkdir -p "$FW/components/core/services/include/services" "$FW/components/core/services/src"
cp "$R"/firmware/components/core/services/include/services/{word_mode.h,wordline.h} \
   "$FW/components/core/services/include/services/"
cp "$R"/firmware/components/core/services/src/{word_mode.c,wordline.c} \
   "$FW/components/core/services/src/"

# 表现层（presentation，唯一出现 lvgl.h 的业务组件）
mkdir -p "$FW/components/ui/presentation/include/presentation" "$FW/components/ui/presentation/src"
cp "$R"/firmware/components/ui/presentation/include/presentation/ui_wordmode.h \
   "$FW/components/ui/presentation/include/presentation/"
cp "$R"/firmware/components/ui/presentation/src/ui_wordmode.c \
   "$FW/components/ui/presentation/src/"

# 编排层（app）
mkdir -p "$FW/components/app/include/app" "$FW/components/app/src/assets" "$FW/components/app/test"
cp "$R"/firmware/components/app/include/app/wordmode_app.h "$FW/components/app/include/app/"
cp "$R"/firmware/components/app/src/wordmode_app.c          "$FW/components/app/src/"
cp "$R"/firmware/components/app/src/assets/{p0_pack.h,p0_audio.h} "$FW/components/app/src/assets/"

# 契约测试 + 备课工具
cp "$R"/firmware/components/app/test/test_wordmode_contract.py "$FW/components/app/test/"
cp "$R"/tools/{gen_lineart.py,gen_audio.py,gen_adpcm.py} "$FW/tools/"
```

---

## 3. 改动 6 个现有文件

按顺序应用 `firmware/patches/`：

| 顺序 | 补丁 | 目标文件 | 改动 |
| --- | --- | --- | --- |
| 1 | `01-ui_plays.h.diff` | `components/ui/presentation/include/presentation/ui_plays.h` | 枚举首位加 `UI_PLAY_WORDMODE`；声明 `ui_plays_set_wordmode_context()` |
| 2 | `02-app_plays.h.diff` | `components/app/include/app/app_plays.h` | 加 3 个入口声明 |
| 3 | `03-CMakeLists.diff` | `components/core/services/CMakeLists.txt`、`components/ui/presentation/CMakeLists.txt` | 挂 3 个新 `.c`；资产目录加入 `PRIV_INCLUDE_DIRS` |
| 4 | `04-app-glue.c` | `app.c` / `app_plays.c` / `ui_plays.c` | 见下 |

`04-app-glue.c` 不是行号 diff（上游 `.c` 体量大、会随版本演进），而是**可直接粘贴的函数 + 明确调用点**，按文件分了 A/B/C/D 四节：

- **A（app.c）**：开机 `app_plays_init()` 成功后注册 `wordmode_app_config_t`；select 回调加 `UI_PLAY_WORDMODE` 分支；`on_button` 里 `HAL_BTN_LONG && index==2` 呼菜单；50 ms 定时器里加 `wordmode_app_pump(now_ms)`。
- **B（app_plays.c）**：实现 `app_plays_open_wordmode()` / `app_plays_wordmode_active()` / `app_plays_wordmode_button()`；在 `app_plays_button()` 最前面分流裸手势。
- **C（ui_plays.c）**：文件作用域加 `s_wordmode_context`，实现 `ui_plays_set_wordmode_context()`，菜单首项文字按上下文切换，焦点初值固定为 0。
- **D**：所需的 include 清单。

> 胶水里用到的 `s_kv` / `s_audio` / `on_play_select` / `on_button` 是占位名，请按上游**实际变量名**替换。所有函数只调用新增模块的公开接口，不触碰上游内部静态状态。

---

## 4. 构建

```bash
. $IDF_PATH/export.sh
cd "$FW"
idf.py build
idf.py size          # 确认 app 分区余量 > 320 KiB（发音包约 280 KiB）
python -m unittest discover -s components/app/test -p 'test_*.py'
python scripts/release.py
python tools/verify_release.py releases/<生成的ZIP> --build-dir build
```

`idf.py build` 首次会拉取 `dependencies.lock` 锁定的 managed_components。

### 若 app 分区余量不足

P0 默认方案 A：`p0_pack.h` / `p0_audio.h` 的 `static const` 编进 `.rodata`，ESP-IDF 默认 Flash XIP 映射、**零 SRAM**。
余量不够时转方案 B：把 20 段 ADP1 打成 `words.bin` 刷入只读分区，用 `esp_partition_mmap` 零拷贝读——但这需要改 `partitions.csv` 和 `release.py` 的六段镜像校验，属于 P1 范围，详见 `docs/p0-prd.md` §3.4。

---

## 5. 真机验收（按顺序过）

- [ ] 冷启动进小智对话页 → **长按 OK** → 菜单第 1 项高亮「单词模式」→ OK → 进入主屏
- [ ] 主屏：线图可辨认、两个 84×52 图案各 ≤ 1/4 屏、**无任何文字**
- [ ] UP 短按 → 认识（大拇哥）→ 1.5 s 自动下一词
- [ ] DOWN 短按 → 不记得（耳朵+循环箭头）→ 慢速重播 → 回主屏
- [ ] OK 短按 → 重播当前词发音
- [ ] 长按 OK → 菜单首项「返回小智」→ OK 回对话页；**长按松手不触发第二次操作**
- [ ] 退出再进入，停在同一词（NVS `wordmode:index` 生效）
- [ ] **断开路由器**也能完整玩（P0 纯离线）
- [ ] 连续按 100 次不重启、不 `task_wdt`；`heap_caps_get_free_size()` 打印进出前后基线
- [ ] **产品验收（最重要）**：让 6 岁小朋友实按，确认「UP=拇指、DOWN=问号」零门槛

> 真机重点看 `banana` 这张线图（纯轮廓仍略像弯叶）与缩小后的两个图案。
> 若三键两图案不成立，P0 的价值就在这一步——改图案或改映射即可，**不牵动架构**。

---

## 6. 实机若发现的两处待核对

1. **菜单触发键**：本方案按 `XIAOZHI_BUILD.txt` 第 4 条「对话页长按确认打开菜单」定为长按 OK。若实机 `app.c` 实现与此不符，以实现为准，同步改 `04-app-glue.c` A4 与契约测试断言，不动架构。
2. **LVGL 绘制 API**：`ui_wordmode.c` 用 LVGL 9.5 的 `LV_EVENT_DRAW_MAIN` + `lv_draw_line()`。若上游 LVGL 配置导致 `d.p1/p2` 类型或 `lv_event_get_layer()` 签名不同，改动集中在 `pen_line()` 一处，其余文件不受影响。

---

## 7. 本仓库无法验证的部分（诚实说明）

沙箱没有 ESP-IDF 5.5.3 工具链、没有 LVGL、没有真机。因此：

- ✅ 已真验证：纯逻辑状态机、笔画解释器（gcc 编译 + 142 用例）、契约测试（11 项）、资产一致性（149 笔 / 775 B / 20 段 ADP1）、4 个 `.c` 的 C99 语法。
- ⚠️ 未验证：`idf.py build` 完整链接、LVGL 实际绘制效果、ADP1 在 ES8311 上的听感、真机堆占用、六段镜像出包。

这些必须按本文第 4/5 节在本地复核。PRD §11.3 列了完整验收项。
