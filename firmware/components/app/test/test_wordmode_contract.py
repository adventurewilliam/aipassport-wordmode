"""单词模式 P0 源码与资产契约测试（照底层仓库 test_*_contract.py 写法）。

运行：
    python -m unittest discover -s firmware/components/app/test -p 'test_*.py'

设计：既能在本独立仓库里跑（检查新增文件 / 补丁 / 资产），也能在把代码
叠加进 ai-passport-xiaozhi-custom 后跑（自动改为检查上游真实文件）。
断言要点来自 p0-prd.md §11.1。
"""
from pathlib import Path
import json
import re
import struct
import unittest

# 本独立仓库：test/ -> app/ -> components/ -> firmware/ -> ROOT
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]

# 若检测到底层固件源码树，则切换到「集成态」路径
UPSTREAM = ROOT / "components" / "app" / "src" / "app.c"
INTEGRATED = UPSTREAM.exists()

if INTEGRATED:
    FW = ROOT
    SRC_SERVICES = FW / "components/core/services/src"
    SRC_PRES = FW / "components/ui/presentation/src"
    INC_PRES = FW / "components/ui/presentation/include/presentation"
    ASSETS = FW / "components/app/src/assets"
    DATA = ASSETS
    PATCHES = None
else:
    FW = ROOT / "firmware"
    SRC_SERVICES = FW / "components/core/services/src"
    SRC_PRES = FW / "components/ui/presentation/src"
    INC_PRES = FW / "components/ui/presentation/include/presentation"
    ASSETS = FW / "components/app/src/assets"
    DATA = ROOT / "assets"
    PATCHES = FW / "patches"


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


class WordmodeContractTest(unittest.TestCase):
    # ---------- 1. 纯逻辑层不得依赖硬件头（保证 PC 可单测） ----------
    def test_pure_logic_layers_are_hardware_free(self) -> None:
        for name in ("word_mode.c", "wordline.c"):
            src = read(SRC_SERVICES / name)
            self.assertNotIn("lvgl.h", src, name)
            self.assertNotIn("#include \"esp_", src, name)
            self.assertNotIn("driver/", src, name)
            self.assertNotIn("lv_canvas", src, name)

    def test_state_machine_signature_is_stable(self) -> None:
        hdr = read(FW / "components/core/services/include/services/word_mode.h")
        for sym in ("word_mode_init", "word_mode_key", "word_mode_tick",
                    "word_mode_poll", "word_mode_index", "word_mode_screen",
                    "WM_ACT_MENU_REQUEST", "WM_SCREEN_UNKNOWN"):
            self.assertIn(sym, hdr)

    # ---------- 2. 表现层禁用位图 / canvas（内存铁律） ----------
    def test_presentation_uses_draw_main_not_bitmaps(self) -> None:
        src = read(SRC_PRES / "ui_wordmode.c")
        self.assertIn("LV_EVENT_DRAW_MAIN", src)
        for banned in ("lv_canvas_create", "lv_image_create",
                       "lv_obj_set_style_shadow", "lv_img_dsc_t",
                       "lv_image_dsc_t", "shadow_width"):
            self.assertNotIn(banned, src, banned)
        self.assertIn("wordline_draw", src)
        self.assertIn("adpcm_decode", src)
        self.assertIn("audio_service_play", src)

    def test_presentation_declares_prd_signatures(self) -> None:
        hdr = read(INC_PRES / "ui_wordmode.h")
        for sym in ("ui_wordmode_open", "ui_wordmode_render",
                    "ui_wordmode_key", "ui_wordmode_play",
                    "ui_wordmode_close", "ui_wordmode_active"):
            self.assertIn(sym, hdr)

    # ---------- 3. 菜单置顶与 C1 按键校订 ----------
    def test_wordmode_is_first_menu_item(self) -> None:
        if INTEGRATED:
            hdr = read(INC_PRES / "ui_plays.h")
        else:
            # 只取 diff 的新增行（排除 "+++" 文件头），否则会先匹配到
            # "-" 旧行里的 UI_PLAY_ANSWER 首位。
            hdr = "\n".join(
                line[1:] for line in
                read(PATCHES / "01-ui_plays.h.diff").splitlines()
                if line.startswith("+") and not line.startswith("+++"))
        m = re.search(r"typedef enum\s*\{([^}]*)\}\s*ui_play_id_t", hdr, re.S)
        self.assertIsNotNone(m, "找不到 ui_play_id_t 枚举")
        first = m.group(1).strip().split(",")[0].strip()
        self.assertEqual(first, "UI_PLAY_WORDMODE", "单词模式必须在枚举首位")

    def test_dynamic_menu_context_hook_exists(self) -> None:
        if INTEGRATED:
            src = read(SRC_PRES / "ui_plays.c")
        else:
            src = read(PATCHES / "04-app-glue.c") + read(PATCHES / "01-ui_plays.h.diff")
        self.assertIn("ui_plays_set_wordmode_context", src)

    def test_no_legacy_down_long_press_mapping(self) -> None:
        """C1 校订：现行映射只有「长按 OK 呼菜单」，不存在「长按 DOWN 呼菜单」。

        注意：docs/p0-prd.md §0 是有意保留的 C1 校订说明与回退开关，
        里面引用旧文案属历史陈述，不能按「全文禁止」误伤；这里只检查
        真正生效的源码映射。
        """
        ui = read(SRC_PRES / "ui_wordmode.c")
        # HAL_BTN_LONG 分支必须判 WM_KEY_OK，且不得出现 DOWN 长按语义
        long_seg = ui.split("case HAL_BTN_LONG:", 1)[1].split("case ", 1)[0]
        self.assertIn("idx == WM_KEY_OK", long_seg)
        self.assertNotIn("WM_KEY_DOWN", long_seg)
        self.assertNotIn("long_press", long_seg)  # 长按由本层直接处理

        if INTEGRATED:
            glue = read(FW / "components/app/src/app.c")
        else:
            glue = read(PATCHES / "04-app-glue.c")
        self.assertIn("HAL_BTN_LONG", glue)
        self.assertIn("index == 2", glue)   # OK 键，不是 DOWN(1)

        # 现行文档仍以「OK 长按 = 呼出菜单」为准
        plan = read(ROOT / "docs/wordmode-plan.html")
        self.assertIn("OK 长按 = 呼出菜单", plan)

    # ---------- 4. 线图资产自洽 ----------
    def test_lineart_assets_are_self_consistent(self) -> None:
        pack = read(ASSETS / "p0_pack.h")
        words = json.loads(read(DATA / "p0_words.json"))
        m = re.search(r"#define\s+WORD_P0_COUNT\s+(\d+)", pack)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), words["totals"]["words"])
        self.assertEqual(words["totals"], {"words": 10, "strokes": 149, "bytes": 775})

        for w in words["words"]:
            self.assertEqual(len(w["strokes"]), w["stroke_count"])
            self.assertEqual(w["binary_size_bytes"], 3 + 5 * w["stroke_count"])
            for s in w["strokes"]:
                self.assertEqual(len(s), 5)
                self.assertIn(s[0], (1, 2, 3, 4))

        # C 数组名与 JSON 词名一致，且含 eye 不含 hand（方案 A 换词）
        names_json = [w["word"] for w in words["words"]]
        names_c = re.findall(r'\{ "(\w+)", "', pack)
        self.assertEqual(names_json, names_c)
        self.assertIn("art_eye", pack)
        self.assertNotIn("art_hand", pack)

    # ---------- 5. 音频资产自洽 ----------
    def test_audio_assets_are_self_consistent(self) -> None:
        audio_h = read(ASSETS / "p0_audio.h")
        manifest = json.loads(read(DATA / "audio/manifest.json"))
        m = re.search(r"#define\s+WORD_AUDIO_COUNT\s+(\d+)", audio_h)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), len(manifest["files"]))
        self.assertEqual(len(manifest["files"]), 10)

        # 每段 .adp1 头 4 字节必须是 "ADP1"（小端 0x31504441）
        adp1_dir = DATA / "audio"
        files = sorted(adp1_dir.glob("*.adp1"))
        self.assertEqual(len(files), 20, "10 词 × 2 版")
        for f in files:
            head = f.read_bytes()[:12]
            magic, rate, ch, _rsv, ns = struct.unpack("<IHBBI", head)
            self.assertEqual(magic, 0x31504441, f.name)
            self.assertEqual(rate, 16000, f.name)
            self.assertEqual(ch, 1, f.name)
            self.assertGreater(ns, 0, f.name)

        # 音频词名与线图词名一致
        self.assertEqual(sorted(manifest["files"]),
                         sorted([w["word"] for w in
                                 json.loads(read(DATA / "p0_words.json"))["words"]]))
        self.assertIn("clip_eye", audio_h)
        self.assertNotIn("clip_hand", audio_h)

    # ---------- 6. 词表零改动（音标不做） ----------
    def test_p1_word_table_unchanged_and_no_new_ipa_field(self) -> None:
        words_dir = DATA / "words"
        units = sorted(words_dir.glob("unit-*.json"))
        self.assertEqual(len(units), 6)
        total = 0
        for f in units:
            d = json.loads(read(f))
            self.assertEqual(len(d["words"]), 10)
            total += len(d["words"])
        self.assertEqual(total, 60)
        idx = json.loads(read(words_dir / "index.json"))
        for forbidden in ("ipa_us", "emoji"):
            self.assertNotIn(forbidden, json.dumps(idx, ensure_ascii=False))

    # ---------- 7. 构建挂接 ----------
    def test_cmake_registers_all_new_sources(self) -> None:
        diff = read(PATCHES / "03-CMakeLists.diff") if not INTEGRATED else ""
        if INTEGRATED:
            s = read(FW / "components/core/services/CMakeLists.txt")
            p = read(FW / "components/ui/presentation/CMakeLists.txt")
        else:
            s = p = diff
        for sym in ("word_mode.c", "wordline.c"):
            self.assertIn(sym, s)
        self.assertIn("ui_wordmode.c", p)
        self.assertIn("assets", p)   # PRIV_INCLUDE_DIRS 指向资产头


if __name__ == "__main__":
    unittest.main(verbosity=2)
