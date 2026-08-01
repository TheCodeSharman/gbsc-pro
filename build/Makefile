# Builds the GBSC-Pro ESP8266 firmware with arduino-cli against an isolated
# data dir. Board, core and library details: README.md.
#
# Needs arduino-cli and make on PATH, and provisions nothing itself, so the
# rules work on any host that can supply them.

HERE   := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
CFG    := $(HERE)/arduino-cli.yaml
SKETCH := $(HERE)/../GBSC-Pro-Source code/gbs-control
OUT    := $(HERE)/output

ARDUINO_CLI ?= arduino-cli
CLI          = $(ARDUINO_CLI) --config-file "$(CFG)"

# GBS_DEBUG compiles in the debug surface -- traces, dumps and the register
# endpoints. On for the lab build; pass GBS_DEBUG=0 for a release-shaped one.
GBS_DEBUG ?= 1

FQBN := esp8266:esp8266:nodemcuv2:xtal=80,eesz=4M2M,ssl=all,exception=legacy,ip=lm2f,vt=flash,dbg=Disabled,lvl=None____,wipe=none

# Not in the Arduino registry, so installed by git URL. The maintained forks
# (now ESP32Async, which GitHub redirects to) add a getParam(const char*)
# overload that makes the sketch's getParam(0) ambiguous; 2ab974a predates it.
#
# Keep the '#' ESCAPED. Make ends the assignment at a bare '#', which drops the
# pin and installs HEAD -- the build then fails in the way this pin prevents.
# `make -p | grep ASYNC_WEB` shows what was really assigned.
#
# ASYNC_TCP is unpinned: no known-good commit was ever recorded for it.
ASYNC_TCP := https://github.com/me-no-dev/ESPAsyncTCP.git
ASYNC_WEB := https://github.com/me-no-dev/ESPAsyncWebServer.git\#2ab974a5b5d65fb69b3fe7e0d4114e97bf7c0703

.PHONY: build setup all clean help check-cli
.DEFAULT_GOAL := build

## build: compile the sketch -> output/gbs-control.ino.bin
build: check-cli
	$(CLI) compile \
	  --fqbn '$(FQBN)' \
	  --build-property 'compiler.cpp.extra_flags=-DGBS_DEBUG=$(GBS_DEBUG)' \
	  --build-path "$(OUT)" \
	  "$(SKETCH)"
	@echo "Firmware: $(OUT)/gbs-control.ino.bin (GBS_DEBUG=$(GBS_DEBUG))"

## setup: one-time (idempotent) install of the esp8266 core + external libs
setup: check-cli
	$(CLI) core update-index
	$(CLI) core install 'esp8266:esp8266@2.6.3'
	$(CLI) lib install \
	  'ESP8266 and ESP32 OLED driver for SSD1306 displays@4.6.2' \
	  'IRremoteESP8266@2.9.0' \
	  'Versatile_RotaryEncoder@1.3.1'
	$(CLI) lib install --git-url '$(ASYNC_TCP)' '$(ASYNC_WEB)'

## all: setup then build
all: setup build

## clean: drop compiled artifacts (keeps the core/lib caches)
clean:
	rm -rf "$(OUT)"

check-cli:
	@command -v $(ARDUINO_CLI) >/dev/null 2>&1 || { \
	  echo "error: $(ARDUINO_CLI) not on PATH — see build/README.md"; \
	  exit 1; }

## help: list targets
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## /  /'
