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

.PHONY: all test geometry sampling memory display-clock output-raster clock-ramp hold-ramp sync-search segment-select register-queue preset-load memory-map sdram-timing memory-bus bringup frame-buffer input-formatter geometry-raster geometry-windows clean help
.DEFAULT_GOAL := test

## help: list the targets
help:
	@grep -E '^## ' "$(HERE)/Makefile" | sed 's/^## /  /'

## geometry: host-compile and run the geometry unit tests
#
# The eleven bench readings from 2026-08-05 are the acceptance criteria here.
#
# The definitions live in src/tv5725/*.cpp, so they have to be compiled in or
# the test links with every method undefined. GLOB the list, never write it out.
#
# Geometry.cpp and Controls.cpp are excluded because they reach for the whole
# sketch, not because they touch registers -- this target links the fake bus,
# so a class that reads the chip is testable here rather than exiled to the
# ESP8266. docs/testing.md.
#
# The `cd` is load-bearing. The sketch directory is "GBSC-Pro-Source code" --
# with a space -- so an absolute glob comes back as a word-split list that no
# amount of quoting inside a make variable survives. Relative to the sketch
# root, every path is space-free.
GEOMETRY_RASTER_SRC = $$(ls src/tv5725/*.cpp | grep -vE '^src/tv5725/(Controls)\.cpp$$')

HOST_GEOMETRY_SRC = $$(ls src/tv5725/*.cpp | grep -vE '^src/tv5725/(Geometry|Controls|FrameBuffer|MemoryBus|InputFormatter|Chip|Gpio|Interrupts|Adc|SyncProcessor|VideoProcessor|BringUp)\.cpp$$')

geometry:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_geometry" \
	  "$(HERE)/test_geometry.cpp" $(HOST_GEOMETRY_SRC)
	@"$(OUT)/test_geometry"

## sampling: host unit tests for the ADC sampling divider and the IF line
#
# PLLAD_MD and IF_HSYNC_RST are one quantity in two registers, and every fault
# here has been something moving one without the other. Pure arithmetic, so it
# costs a second to check.
sampling:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_sampling" \
	  "$(HERE)/test_sampling.cpp" src/tv5725/Sampling.cpp src/tv5725/InputLine.cpp
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

## output-raster: host unit tests for the computed output raster
#
output-raster:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_output_raster" \
	  "$(HERE)/test_output_raster.cpp" src/tv5725/OutputRaster.cpp src/tv5725/DisplayClock.cpp
	@"$(OUT)/test_output_raster"

## clock-ramp: host unit tests for how the external clock is allowed to move
#
# The pure half of the Si5351 board logic: the 1 kHz ramp and the two preload
# frequencies, both carried over from the sketch unexplained and untested. The
# device half stays in the .ino because it needs Wire.
clock-ramp:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_clock_ramp" \
	  "$(HERE)/test_clock_ramp.cpp" src/clock/ClockRamp.cpp
	@"$(OUT)/test_clock_ramp"

## sync-search: host unit tests for which sync search a saved input selects
#
# The V-sync-absent search was gated on S_RGBs while the V-sync-present one
# accepted S_VGA too, so a unit saved as VGA on a source reading VSACT 0 matched
# neither and detection livelocked -- ADC_INPUT_SEL flipping 1/0 on a ~1 s beat
# forever. Pure logic, so it costs a millisecond here and a power cycle there.
sync-search:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_sync_search" \
	  "$(HERE)/test_sync_search.cpp" src/input/SyncSearch.cpp
	@"$(OUT)/test_sync_search"

## preset-load: host unit tests for the mode state a preset load decides
#
# The non-byte half of writeProgramArrayNew(). It has to outlive the twelve
# tables it is currently buried in, so it is a class over plain integers with
# no chip and no globals -- and therefore checkable here rather than by
# flashing a board and reading rto-> back out of a log.
preset-load:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_preset_load" \
	  "$(HERE)/test_preset_load.cpp" src/tv5725/PresetLoad.cpp
	@"$(OUT)/test_preset_load"

## memory-map: host unit tests for the SDRAM layout and the capture capacity
#
# Whether the frame the engine is about to capture fits in the buffer the chip
# was told to write it into -- a question nothing in this firmware has ever
# asked. Pure address arithmetic, so it costs a second here against a flash
# cycle and a corrupted picture there.
memory-map:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_memory_map" \
	  "$(HERE)/test_memory_map.cpp" src/tv5725/MemoryMap.cpp
	@"$(OUT)/test_memory_map"

## sdram-timing: host unit tests for how fast the SDRAM bus may run
#
# Derives the memory clock from the EM638325TS-6's tCK, tRCD and tRP, so
# "fastest in spec" is arithmetic against a datasheet rather than a number
# somebody chose.
sdram-timing:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_sdram_timing" \
	  "$(HERE)/test_sdram_timing.cpp" src/tv5725/SdramTiming.cpp
	@"$(OUT)/test_sdram_timing"

## memory-bus: host unit tests for the SDRAM bus bring-up, off the board
#
# The first subsystem bring-up testable without hardware. tw.h is pure C++ whose
# only Arduino dependency is Wire, so test/fake/Wire.h -- already here for
# segment-select -- is the entire seam, and MemoryBus.cpp compiles unchanged.
#
# The include flags are load-bearing: -I test/fake is what makes tw.h's
# `#include <Wire.h>` resolve to the fake, and -I test is what lets the test say
# "fake/Wire.h" to define the instance.
memory-bus:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_memory_bus" \
	  "$(HERE)/test_memory_bus.cpp" src/tv5725/MemoryBus.cpp src/tv5725/SdramTiming.cpp
	@"$(OUT)/test_memory_bus"

## bringup: host unit tests for the whole static bring-up, and its --dump
#
# The ONE test that spans every bring-up class, which is what makes it the
# behaviour-preservation proof for retiring Tv5725::Bringup::. Each subsystem
# graduates into an ordinary Tv5725::<Subsystem>::init() one commit at a time,
# every one of those commits claims to be a pure move, and
#
#     ./output/test_bringup --dump
#
# is how the claim is checked: every register the bring-up touches, in order,
# so a reordering shows up here instead of on the bench. Ordering is not
# cosmetic -- the six SFTRST_*_RSTZ resets must be released before the blocks
# they hold are configured, which is a constraint BETWEEN classes that no
# per-class view can see.
#
# GLOBBED, never written out, for the reason the geometry target gives: a
# hand-written source list drifted within an hour of being written. Controls.cpp
# and Geometry.cpp are the only exclusions -- the first needs more of Arduino than
# the fake provides, the second calls getSourceFieldRate(), which lives in the
# sketch and not in src/.
BRINGUP_SRC = $$(ls src/tv5725/*.cpp | grep -vE '^src/tv5725/(Geometry|Controls)\.cpp$$')

bringup:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_bringup" \
	  "$(HERE)/test_bringup.cpp" $(BRINGUP_SRC)
	@"$(OUT)/test_bringup"

## frame-buffer: host unit tests for the SDRAM map and the FIFO watermarks
#
# Same fake-Wire seam as memory-bus. Added when the FIFO request watermarks
# acquired an owner: they are mode-dependent, so the generated bring-up block
# can never emit them, and until this test existed nothing could tell a written
# watermark from one the preset table happened to leave behind.
frame-buffer:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_frame_buffer" \
	  "$(HERE)/test_frame_buffer.cpp" src/tv5725/FrameBuffer.cpp src/tv5725/MemoryMap.cpp
	@"$(OUT)/test_frame_buffer"

## input-formatter: host unit tests for the input formatter's owned fields
#
# One field today -- IF_LD_ST, the last gap-report name outside segment 4 -- and
# the landing site for the rest of Bringup::InputFormatter when it graduates.
# Same fake-Wire seam as frame-buffer.
input-formatter:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_input_formatter" \
	  "$(HERE)/test_input_formatter.cpp" src/tv5725/InputFormatter.cpp
	@"$(OUT)/test_input_formatter"

## geometry-raster: host unit tests for Geometry::solveRaster()'s refuse/defer split
#
# The geometry engine, off the board. Geometry.cpp was excluded from every host
# build for touching GBS:: and including <Arduino.h>; fake/Wire.h covered the
# first and fake/Arduino.h -- three lines, for `boolean` -- covers the second.
# getSourceFieldRate() is the sketch's, and the test supplies its own, which is
# what makes the source measurement holdable still.
geometry-raster:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_geometry_raster" \
	  "$(HERE)/test_geometry_raster.cpp" $(GEOMETRY_RASTER_SRC)
	@"$(OUT)/test_geometry_raster"

## geometry-windows: host unit tests for the registers one Geometry::apply() writes
#
# The net under deleting doPostPresetLoadSteps()'s geometry writes. Those all sit
# behind videoStandardInput 1-4, 8 or 9, and the bench source comes up as 14, so
# no bench test can reach them -- see the header of test_geometry_windows.cpp.
geometry-windows:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_geometry_windows" \
	  "$(HERE)/test_geometry_windows.cpp" $(GEOMETRY_RASTER_SRC)
	@"$(OUT)/test_geometry_windows"

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
test: geometry sampling memory display-clock output-raster clock-ramp hold-ramp sync-search segment-select register-queue preset-load memory-map sdram-timing memory-bus bringup frame-buffer input-formatter geometry-raster geometry-windows
all: test

## clean: remove the compiled test binaries
clean:
	rm -rf "$(OUT)"

