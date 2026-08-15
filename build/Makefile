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

# BOOTLOG_BYTES: RAM for the boot trace served at /bootlog. Default 0 -- OFF.
# At 2048 it costs ~2160 bytes of globals, which is enough to put free heap
# under the threshold SerialMirror needs before it will broadcast, so the web
# console goes silent. Turn it on only for a boot-time investigation:
#   make -C build flash BOOTLOG_BYTES=2048
BOOTLOG_BYTES ?= 0

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

# Flashing. PORT is the USB serial adapter; HOST is the unit's address for OTA.
# Both are overridable: `make -C build flash PORT=/dev/ttyUSB1`.
PORT ?= /dev/ttyUSB0
HOST ?= 192.168.88.108

.PHONY: build setup all clean help check-cli flash flash-ota
.DEFAULT_GOAL := build

## build: compile the sketch -> output/gbs-control.ino.bin
build: check-cli
	$(CLI) compile \
	  --fqbn '$(FQBN)' \
	  --build-property 'compiler.cpp.extra_flags=-DGBS_DEBUG=$(GBS_DEBUG) -DBOOTLOG_BYTES=$(BOOTLOG_BYTES)' \
	  --build-path "$(OUT)" \
	  "$(SKETCH)"
	@echo "Firmware: $(OUT)/gbs-control.ino.bin (GBS_DEBUG=$(GBS_DEBUG) BOOTLOG_BYTES=$(BOOTLOG_BYTES))"

## flash: build, then upload over USB serial (PORT=/dev/ttyUSB0)
#
# The FQBN carries wipe=none, so SPIFFS survives: the stored timings and
# preferences are still there after a flash. Do not "fix" that to wipe=all
# without meaning it.
#
# Opening the serial port toggles DTR/RTS and resets the board. If you are also
# watching the console, settle the port first or the reset will look like a
# firmware fault:
#
#     stty -F $(PORT) 115200 -hupcl raw -echo
#
# The boot ROM prints at 74880 baud, not 115200, so early boot output is garbage
# at the normal rate.
flash: build
	$(CLI) upload \
	  --fqbn '$(FQBN)' \
	  --port '$(PORT)' \
	  --input-dir "$(OUT)" \
	  "$(SKETCH)"

## flash-ota: build, then upload over WiFi (HOST=<ip>) -- no cable needed
#
# The firmware runs ArduinoOTA (hostname "GBS OTA"). Faster than fetching a
# cable, but a failed OTA leaves you needing one anyway, so prefer `flash` when
# the change touches early boot or WiFi setup.
#
# Close the web UI first: the unit permits one WebSocket client, and OTA plus a
# browser is two.
flash-ota: build
	$(CLI) upload \
	  --fqbn '$(FQBN)' \
	  --port '$(HOST)' \
	  --protocol network \
	  --input-dir "$(OUT)" \
	  "$(SKETCH)"

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
