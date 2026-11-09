#!/bin/sh
# host/verify.sh —— 一键跑通所有「PC 侧可验证」的检查
#   cd host && sh verify.sh
# 覆盖：2 个纯逻辑单测（gcc 真编译真跑）+ 契约测试 + 4 个 .c 的语法检查。
# 不含：真机 idf.py build / 烧录 / 产品验收（需 ESP-IDF 5.5.3 + 硬件）。
set -e
cd "$(dirname "$0")"
ROOT=..

echo "=============================================="
echo " 1/3 纯逻辑单测（gcc -Wall -Wextra -Werror）"
echo "=============================================="
make clean >/dev/null 2>&1 || true
make check

echo
echo "=============================================="
echo " 2/3 契约测试（Python unittest）"
echo "=============================================="
(cd "$ROOT" && python3 -m unittest discover \
    -s firmware/components/app/test -p 'test_*.py' -v)

echo
echo "=============================================="
echo " 3/3 固件 .c 语法检查（LVGL/IDF 用 stub 头）"
echo "=============================================="
INC_STUBS="-I. -Istubs"
INC_REAL="-I$ROOT/firmware/components/core/services/include \
          -I$ROOT/firmware/components/ui/presentation/include \
          -I$ROOT/firmware/components/app/include \
          -I$ROOT/firmware/components/app/src/assets"
CF="-std=c99 -Wall -Wextra -Werror -fsyntax-only"

# zsh 不做默认分词，这里用显式展开；用 sh 执行时可安全分词
# shellcheck disable=SC2086
for f in \
  "$ROOT/firmware/components/ui/presentation/src/ui_wordmode.c" \
  "$ROOT/firmware/components/app/src/wordmode_app.c"; do
    echo "  check $(basename "$f")"
    gcc $CF $INC_STUBS $INC_REAL "$f"
done
for f in \
  "$ROOT/firmware/components/core/services/src/word_mode.c" \
  "$ROOT/firmware/components/core/services/src/wordline.c"; do
    echo "  check $(basename "$f")"
    gcc $CF -I"$ROOT/firmware/components/core/services/include" "$f"
done
echo "  4 个 .c 语法检查通过"

echo
echo "=============================================="
echo " 全部 PC 侧检查通过"
echo " 下一步：见 integration/INTEGRATION.md 执行真机 idf.py build"
echo "=============================================="
