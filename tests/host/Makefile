# Host tests: plain C checks of Booster's pure logic, built with the host
# compiler (no Pico SDK) and run in seconds.
#
#   make -C tests/host test         # build and run everything; fails on any failure
#   make -C tests/host known-bugs   # the defects the firmware still has
#   make -C tests/host clean
#
# Every test_*.c here is one program: it compiles the firmware units it tests
# from booster/src, with shim/ standing in for the few SDK headers they pull
# in, and exits non-zero when a check fails. Third-party code the units call
# is the real thing from the submodules (mbedtls's base64 decoder and the
# CYW43 driver's constants from pico-sdk), so the tests check what the
# firmware runs. Address and undefined-behaviour sanitizers make a write past
# a buffer fail the test instead of passing by luck.
#
# A bug_*.c test shows a defect the firmware still has: it fails today, and
# `known-bugs` passes only while every one of them does. When the defect is
# fixed the test starts passing: rename it to test_*.c so it guards the fix.
#
# test_default_keys.py checks that Booster's and the core firmware's settings
# defaults hold the same keys (it reads rp2-atarist-rpikb, the submodule).
#
# Needs the pico-sdk submodule with its lib/mbedtls and lib/cyw43-driver, and
# rp2-atarist-rpikb; CI fetches only those.

CC ?= cc
PYTHON ?= python3
REPO := ../..
BUILD := build

# -fshort-enums: the firmware's ARM EABI stores an enum in the smallest type
# that holds it, which makes a settings entry 127 bytes (30 key, 1 type,
# 96 value). The host compiler's default 4-byte enum would test another
# layout. gnu11 is the dialect the firmware itself is built with.
# The SDK and firmware headers are included as system headers: their style is
# clang-tidy's job, and they do not all build warning-free on the host.
CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Wno-unknown-pragmas -fshort-enums \
          -fsanitize=address,undefined -fno-omit-frame-pointer \
          -fno-sanitize-recover=undefined \
          -DBOARD_TARGET=2 -DPICO_RP2350=1 \
          '-DMBEDTLS_CONFIG_FILE="mbedtls_config.h"' \
          -I shim -isystem $(REPO)/booster/src/include \
          -isystem $(REPO)/booster/src/settings -isystem $(REPO)/booster/src \
          -isystem $(REPO)/pico-sdk/lib/mbedtls/include \
          -isystem $(REPO)/pico-sdk/lib/cyw43-driver/src
LDFLAGS := -fsanitize=address,undefined -pthread

C_TESTS := $(patsubst %.c,$(BUILD)/%,$(wildcard test_*.c))
BUG_TESTS := $(patsubst %.c,$(BUILD)/%,$(wildcard bug_*.c))

.PHONY: test known-bugs clean FORCE

test: $(C_TESTS)
	@set -e; for t in $(C_TESTS); do echo "== $$t"; ./$$t; done
	@echo "== test_default_keys.py"; $(PYTHON) test_default_keys.py

known-bugs: $(BUG_TESTS)
	@set -e; for t in $(BUG_TESTS); do echo "== $$t (must fail)"; \
	  if ./$$t > $$t.log 2>&1; then echo "$$t passes: the defect is fixed, rename it to test_*"; exit 1; \
	  else tail -3 $$t.log; fi; done

# A test's own firmware sources are listed in its first line, relative to the
# repository root:
#   // units: booster/src/httpd_params.c booster/src/cjson/cJSON.c
# The test itself is held to -Werror; the units it compiles are the
# firmware's and third parties' code, built as the firmware has them, with
# warnings off (clang-tidy covers the firmware's style).
# Always rebuilt (it takes about a second): a test must never pass on a binary
# older than the firmware source it checks.
$(BUILD)/%: %.c FORCE | $(BUILD)
	$(CC) $(CFLAGS) -Werror -c -o $@.o $<
	$(CC) $(CFLAGS) -w -o $@ $@.o \
	  $(addprefix $(REPO)/,$(shell sed -n '1s|^// units:||p' $<)) $(LDFLAGS)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

FORCE:
