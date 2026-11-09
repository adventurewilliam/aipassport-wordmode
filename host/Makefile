# host/Makefile —— PC 侧纯逻辑单测（无需 ESP-IDF / LVGL / 真机）
#
#   make         编译并运行全部单测
#   make clean   清理产物
#
# 这些测试只覆盖「不依赖硬件」的层：状态机 word_mode.c、笔画解释器 wordline.c。
# UI / 音频 / 模式切换依赖 ESP-IDF 5.5.3 + LVGL 9.5，需在真机用 idf.py build 复核，
# 见 firmware/patches/ 与 integration/INTEGRATION.md。

CC      ?= gcc
CFLAGS  := -std=c99 -Wall -Wextra -Werror -O2
INC     := -I../firmware/components/core/services/include \
           -I../firmware/components/app/src/assets
SRC_DIR := ../firmware/components/core/services/src

TESTS   := test_word_mode test_wordline

.PHONY: all clean check
all: check

check: $(TESTS)
	@echo "---- 运行 word_mode 状态机单测 ----"
	@./test_word_mode
	@echo "---- 运行 wordline 解释器单测 ----"
	@./test_wordline
	@echo "==== 全部 PC 单测通过 ===="

test_word_mode: test_word_mode.c $(SRC_DIR)/word_mode.c \
                ../firmware/components/core/services/include/services/word_mode.h
	$(CC) $(CFLAGS) $(INC) test_word_mode.c $(SRC_DIR)/word_mode.c -o $@

test_wordline: test_wordline.c $(SRC_DIR)/wordline.c \
               ../firmware/components/core/services/include/services/wordline.h \
               ../firmware/components/app/src/assets/p0_pack.h
	$(CC) $(CFLAGS) $(INC) test_wordline.c $(SRC_DIR)/wordline.c -o $@

clean:
	rm -f $(TESTS)
