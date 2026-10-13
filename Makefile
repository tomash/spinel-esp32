# spinel-esp32 -- top-level entry point.
#
# Wraps the tools/ scripts so the common paths are one word. Everything still
# works if you call tools/build-app.sh directly; this is a convenience layer.
#
#   make setup                    one-time: submodule, host spinel, cross runtime
#   make run   APP=app/hello.rb   Ruby -> C -> host binary -> run
#   make emit  APP=app/blink.rb   Ruby -> C for the ESP32 (no build)
#   make rt                       cross-build libspinel_rt (riscv32 default)
#   make flash APP=app/blink.rb   emit + idf.py build + flash + serial capture
#   make monitor APP=app/blink.rb emit + build + flash + capture serial
#   make size                     report the linked image size
#   make clean                    remove build/ (keeps vendor/spinel artifacts)
#
# Variables:
#   APP     path to the .rb (required for run/emit/flash/monitor)
#   TARGET  esp32c3 (default) | esp32   -- which board to build for
#   PORT    serial device (default /dev/ttyUSB0; also $SPINEL_ESP_PORT)
#   RT      riscv32 (default) | esp32   -- which runtime archive to build
#
# The tools/ scripts own the real logic (and the IDF/toolchain discovery);
# this Makefile only maps friendly targets onto them.

APP    ?=
TARGET ?= esp32c3
PORT   ?= $(if $(SPINEL_ESP_PORT),$(SPINEL_ESP_PORT),/dev/ttyUSB0)
RT     ?= riscv32
JOBS   ?= $(shell nproc 2>/dev/null || echo 2)

SHELL := /bin/bash
BUILD_APP := tools/build-app.sh
BUILD_RT  := tools/build-rt.sh

.PHONY: help setup all deps host run emit rt rt-esp32 flash monitor size clean dist

help:
	@sed -n '2,28p' Makefile | sed 's/^# \{0,1\}//'

# ---- setup ----------------------------------------------------------------
setup:
	tools/bootstrap.sh

deps:
	make -C vendor/spinel deps

host: run

all: run

# ---- host pipeline --------------------------------------------------------
run:
	@test -n "$(APP)" || { echo "usage: make run APP=app/hello.rb"; exit 2; }
	$(BUILD_APP) $(APP)

emit:
	@test -n "$(APP)" || { echo "usage: make emit APP=app/blink.rb [TARGET=esp32]"; exit 2; }
	$(BUILD_APP) $(APP) --target=$(TARGET) --emit-only

# ---- ESP32 cross runtime --------------------------------------------------
rt:
	$(BUILD_RT) $(RT)

rt-esp32:
	$(BUILD_RT) esp32

# ---- firmware: build + flash + capture ------------------------------------
flash:
	@test -n "$(APP)" || { echo "usage: make flash APP=app/blink.rb [TARGET=esp32c3|esp32] [PORT=/dev/ttyUSB0]"; exit 2; }
	SPINEL_ESP_PORT=$(PORT) $(BUILD_APP) $(APP) --target=$(TARGET) --flash

monitor:
	@test -n "$(APP)" || { echo "usage: make monitor APP=app/blink.rb [TARGET=esp32c3|esp32] [PORT=/dev/ttyUSB0]"; exit 2; }
	SPINEL_ESP_PORT=$(PORT) $(BUILD_APP) $(APP) --target=$(TARGET) --flash --monitor

size:
	@bin=firmware/build/spinel_esp32_app.bin; \
	if [ -f "$$bin" ]; then ls -l "$$bin" | awk '{print $$5" bytes  "$$9}'; \
	else echo "no image yet -- run 'make flash APP=...' first"; fi

# ---- housekeeping ---------------------------------------------------------
clean:
	rm -rf build
	rm -f firmware/main/app.c
	rm -rf firmware/build

dist:
	@echo "release assets are cut by tools/release-assets.sh (see docs/packaging.md)"
