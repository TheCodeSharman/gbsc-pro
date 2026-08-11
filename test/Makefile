# Host-compiled unit tests. `make -C test`, or `make -C build test`, which
# delegates here so the command in CLAUDE.md keeps working.
#
# Requires g++ and doctest on PATH.
# Like build/Makefile, this deliberately does NOT shell out to nix -- the
# environment is the caller's job, so the rules work outside NixOS.

HERE   := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SKETCH := $(HERE)/../GBSC-Pro-Source code/gbs-control
OUT    := $(HERE)/output

CXX ?= g++
CXXFLAGS ?= -std=c++11 -O1 -Wall -Wextra

.PHONY: all test geometry sampling memory display-clock hold-ramp segment-select register-queue clean help
.DEFAULT_GOAL := test

## help: list the targets
help:
	@grep -E '^## ' "$(HERE)/Makefile" | sed 's/^## /  /'

## geometry: host-compile and run the geometry unit tests
#
# The geometry is pure arithmetic with no Arduino dependency, precisely so this
# is possible: the alternative is flashing a board over USB to find out whether
# a formula is right. Seconds here against minutes there, and the eleven bench
# readings from 2026-08-05 are acceptance criteria rather than a memory.
#
# The definitions live in src/tv5725/*.cpp, so they have to be compiled in or
# the test links with every method undefined. The list is GLOBBED, never written
# out.
#
# Geometry.cpp and Controls.cpp are excluded: they touch GBS registers and
# Arduino, so they build for the ESP8266 only.
#
# The `cd` is load-bearing. The sketch directory is "GBSC-Pro-Source code" --
# with a space -- so an absolute glob comes back as a word-split list that no
# amount of quoting inside a make variable survives. Relative to the sketch
# root, every path is space-free.
HOST_GEOMETRY_SRC = $$(ls src/tv5725/*.cpp | grep -vE '^src/tv5725/(Geometry|Controls)\.cpp$$')

geometry:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_geometry" \
	  "$(HERE)/test_geometry.cpp" $(HOST_GEOMETRY_SRC)
	@"$(OUT)/test_geometry"

## sampling: host unit tests for the ADC sampling divider and the IF line
#
# PLLAD_MD and IF_HSYNC_RST are one quantity in two registers, and every fault
# here has been something moving one without the other. Pure arithmetic, so it
# costs a second to check.
sampling:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_sampling" \
	  "$(HERE)/test_sampling.cpp" src/tv5725/Sampling.cpp
	@"$(OUT)/test_sampling"

## memory: host unit tests for the playback burst structure
#
# PB_FETCH_NUM sets the memory read request rate, which is one side of the beat
# that tore the picture across 80 of 493 framings. Pure arithmetic.
memory:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_memory" \
	  "$(HERE)/test_memory.cpp" src/tv5725/Memory.cpp
	@"$(OUT)/test_memory"

## hold-ramp: host unit tests for the IR hold-to-accelerate ramp
hold-ramp:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_hold_ramp" \
	  "$(HERE)/test_hold_ramp.cpp" src/input/HoldRamp.cpp
	@"$(OUT)/test_hold_ramp"

## segment-select: host unit tests for the TV5725 segment pointer in tw.h
#
# tw.h needs only <Wire.h>, so test/fake supplies one backed by an in-memory
# model of the six register banks behind the pointer at 0xF0. That is what makes
# this testable off the board at all: the question is which bank a write landed
# in, and the fake can answer it where an HTTP register read never could.
segment-select:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" -o "$(OUT)/test_segment_select" \
	  "$(HERE)/test_segment_select.cpp"
	@"$(OUT)/test_segment_select"

## display-clock: host unit tests for the display clock and the raster it affords
#
# A preset's raster demands htotal x frameLines x fieldRate hertz, so the
# datasheet's CLKOUT ceiling is a constraint on which rasters are legal -- and
# pal_1920x1080 spends 81 of the 108 MHz available. Pure arithmetic, which is the
# cheapest possible place to check it.
display-clock:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_display_clock" \
	  "$(HERE)/test_display_clock.cpp" src/tv5725/DisplayClock.cpp
	@"$(OUT)/test_display_clock"


## register-queue: host unit tests for the deferred register queue
#
# The queue is plain bookkeeping with no Arduino dependency, deliberately: the
# delicate part is a request object that the async side can delete while loop()
# holds it, and that is far cheaper to get right here than on the board.
register-queue:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_register_queue" \
	  "$(HERE)/test_register_queue.cpp" src/net/RegisterQueue.cpp
	@"$(OUT)/test_register_queue"

## test: every host unit test
test: geometry sampling memory display-clock hold-ramp segment-select register-queue
all: test

## clean: remove the compiled test binaries
clean:
	rm -rf "$(OUT)"
