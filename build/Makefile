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

# Used by `setup` to install the core and by `flash-ota` to find espota.py
# inside it. One place, so an upgrade cannot leave the OTA path pointing at a
# version that is no longer installed.
CORE_VERSION := 2.6.3

# GBS_DEBUG compiles in the debug surface -- traces, dumps and the register
# endpoints. On for the lab build; pass GBS_DEBUG=0 for a release-shaped one.
GBS_DEBUG ?= 1

# BOOTLOG_BYTES: RAM for the boot trace served at /bootlog. Default 0 -- OFF.
# At 2048 it costs ~2160 bytes of globals, which is enough to put free heap
# under the threshold SerialMirror needs before it will broadcast, so the web
# console goes silent. Turn it on only for a boot-time investigation:
#   make -C build flash BOOTLOG_BYTES=2048
BOOTLOG_BYTES ?= 0

# Trace every TV5725 register write to the UART. Off by default: it is a lot of
# output and it slows the loop, but it is the only way to see the *sequence* of
# writes, and an HTTP sampler cannot -- it missed the entry into the HPERIOD_IF
# fault twice at 3 Hz. Costs no heap: Serial, not SerialM. See tw.h rawWrite.
#   make -C build flash GBS_TRACE_WRITES=1
GBS_TRACE_WRITES ?= 0

# Compile in /samplinglog, the divider sweep and the per-sample register stream.
# Off even when GBS_DEBUG is on, and separately switchable because it is the one
# piece of the debug surface that WRITES: sweep mode latches PLLAD_MD through
# Adc::applySampleRate, which makes it a second owner of the divider alongside
# the geometry engine, and a sweep cut short by a reset leaves the divider where
# it stopped -- a green screen with every register self-consistent. Turn it on
# only for a clock-measurement investigation, and only with GBS_DEBUG=1, which
# the sketch enforces:
#   make -C build flash-ota HOST=... GBS_SAMPLING_LOG=1
GBS_SAMPLING_LOG ?= 0


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

# OTA. The unit listens on UDP OTA_DEVICE_PORT, and espota listens on TCP
# OTA_HOST_PORT *here* for the unit to connect back -- so OTA_HOST_PORT is the
# one a host firewall has to allow inbound. docs/ota-flashing.md.
OTA_DEVICE_PORT ?= 8266
OTA_HOST_PORT   ?= 8266
ESPOTA := $(HERE)/data/packages/esp8266/hardware/esp8266/$(CORE_VERSION)/tools/espota.py

.PHONY: build setup all clean help check-cli flash flash-ota webui webui-check test \
        test-geometry
.DEFAULT_GOAL := build

CXX ?= g++
CXXFLAGS ?= -std=c++11 -O1 -Wall -Wextra

PYTHON ?= python3
WEBUI_SRC := $(SKETCH)/webui.html
WEBUI_HDR := $(SKETCH)/webui_html.h

## webui: regenerate webui_html.h from webui.html
#
# The firmware serves the UI from a gzipped PROGMEM array, not from the
# filesystem, so editing webui.html alone changes nothing on the device.
webui:
	$(PYTHON) "$(HERE)/webui_header.py" --source "$(WEBUI_SRC)" --header "$(WEBUI_HDR)"

## webui-check: fail if webui_html.h is stale (build depends on this)
webui-check:
	@$(PYTHON) "$(HERE)/webui_header.py" --source "$(WEBUI_SRC)" --header "$(WEBUI_HDR)" --check

## test: every host unit test (delegates to test/)
#
# The rules are in test/Makefile; add nothing here.
test test-geometry test-hold-ramp:
	@$(MAKE) --no-print-directory -C "$(HERE)/../test" \
	  $(patsubst test-%,%,$(patsubst test,test,$@))

## build: compile the sketch -> output/gbs-control.ino.bin
build: check-cli webui-check
	$(CLI) compile \
	  --fqbn '$(FQBN)' \
	  --build-property 'compiler.cpp.extra_flags=-DGBS_DEBUG=$(GBS_DEBUG) -DGBS_TRACE_WRITES=$(GBS_TRACE_WRITES) -DBOOTLOG_BYTES=$(BOOTLOG_BYTES) -DGBS_SAMPLING_LOG=$(GBS_SAMPLING_LOG)' \
	  --build-path "$(OUT)" \
	  "$(SKETCH)"
	@echo "Firmware: $(OUT)/gbs-control.ino.bin (GBS_DEBUG=$(GBS_DEBUG) GBS_TRACE_WRITES=$(GBS_TRACE_WRITES) BOOTLOG_BYTES=$(BOOTLOG_BYTES) GBS_SAMPLING_LOG=$(GBS_SAMPLING_LOG))"

## flash: build, then upload over USB serial (PORT=/dev/ttyUSB0)
#
# The FQBN carries wipe=none, so the filesystem survives: the stored timings and
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

## flash-ota: build, arm the unit, then upload over WiFi (HOST=<ip>)
#
# Arms the unit itself: allowUpdatesOTA defaults to false and a reboot disarms
# it again, so it was a manual step and forgetting it looked like a dead unit.
#
# Calls espota directly rather than `arduino-cli upload --protocol network`,
# which lets espota pick a random host port in 10000-60000. The unit connects
# BACK to that port, so pinning it means the host firewall opens one port
# instead of fifty thousand. When an upload hangs, ota_probe.py says which half
# of the handshake failed. docs/ota-flashing.md.
#
# A unit that has stopped answering HTTP cannot be armed, which is exactly when
# you want to reflash -- so keep a cable to hand, and prefer `flash` when the
# change touches early boot or WiFi setup.
flash-ota: build
	curl -fsS -o /dev/null 'http://$(HOST)/sc?c'
	sleep 1
	$(PYTHON) "$(ESPOTA)" \
	  --ip '$(HOST)' \
	  --port $(OTA_DEVICE_PORT) \
	  --host_port $(OTA_HOST_PORT) \
	  --file "$(OUT)/gbs-control.ino.bin" \
	  --progress

## setup: one-time (idempotent) install of the esp8266 core + external libs
setup: check-cli
	$(CLI) core update-index
	$(CLI) core install 'esp8266:esp8266@$(CORE_VERSION)'
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
