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

.PHONY: all test sampling-log axis source-key source-timing framing-table framing-text slot-table slot-text scale input-line active-image video-processor-timings geometry source-measurement memory display-clock output-raster clock-ramp rate-agreement hold-ramp sync-search input-source segment-select register-queue preset-load memory-map sdram-timings memory-bus bringup frame-buffer input-formatter hd-bypass mode-detect deinterlacer interrupts field-request geometry-raster geometry-windows controls adc source-standard clean help
.DEFAULT_GOAL := test

## help: list the targets
help:
	@grep -E '^## ' "$(HERE)/Makefile" | sed 's/^## /  /'

# The definitions live in src/tv5725/*.cpp, so they have to be compiled in or
# the test links with every method undefined. GLOB the list, never write it out.
#
# Every target here links the fake bus, so a class that reads the chip is
# testable on the host rather than exiled to the ESP8266. docs/testing.md.
# Controls.cpp is left out of the lists below only because it needs a Print,
# which the `controls` target supplies and these have no use for.
#
# The `cd` is load-bearing. The sketch directory is "GBSC-Pro-Source code" --
# with a space -- so an absolute glob comes back as a word-split list that no
# amount of quoting inside a make variable survives. Relative to the sketch
# root, every path is space-free.
# Tv5725::DisplayClock steers the Si5351 through Clock::ClockGen, so every list
# that links DisplayClock.cpp links the clock with it.
CLOCK_SRC = src/clock/ClockGen.cpp src/clock/ClockRamp.cpp

GEOMETRY_RASTER_SRC = $$(ls src/tv5725/*.cpp | grep -vE '^src/tv5725/(Controls)\.cpp$$') $(CLOCK_SRC)

HOST_GEOMETRY_SRC = $$(ls src/tv5725/*.cpp | grep -vE '^src/tv5725/(Geometry|Controls|FrameBuffer|MemoryBus|InputFormatter|Chip|Gpio|Interrupts|Adc|SamplingLog|SourceStandard|SyncProcessor|VideoProcessor|BringUp)\.cpp$$') $(CLOCK_SRC)

# One target per class the geometry is built from. The eleven bench readings
# from 2026-08-05 are the acceptance criteria across them.
#
# $(1) is the class's test, $(2) the target name.
define geometry_unit
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/$(1)" \
	  "$(HERE)/$(1).cpp" $(HOST_GEOMETRY_SRC)
	@"$(OUT)/$(1)"
endef

## axis: host unit tests for Tv5725::Axis, and its --dump
#
# Where the scaler starts writing, how far it may magnify, and the room the
# raster leaves -- the arithmetic every window is placed by. `--dump` prints the
# solved grid, which is how a behaviour-preserving refactor is proven.
axis:
	$(call geometry_unit,test_axis)

## scale: host unit tests for Tv5725::Scale
#
# produced IS capture x 1024 / scale. It did not look like a pure multiply
# because the deficit against it changes sign with magnification.
scale:
	$(call geometry_unit,test_scale)

## source-key: host unit tests for Tv5725::SourceKey
#
# What identifies a source, so a framing can be kept against it across an
# output change and stored against it. docs/framing-presets.md.
source-key:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_source_key" \
	  "$(HERE)/test_source_key.cpp" src/tv5725/SourceKey.cpp \
	  src/tv5725/SourceMeasurement.cpp src/tv5725/InputLine.cpp
	@"$(OUT)/test_source_key"

## source-timing: host unit tests for Tv5725::SourceTiming
#
# Where a published raster puts active video, for a source that emits one.
# docs/investigations/vesa-modes-are-clipped-by-default.md.
source-timing:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_source_timing" \
	  "$(HERE)/test_source_timing.cpp" src/tv5725/SourceTiming.cpp \
	  src/tv5725/SourceKey.cpp src/tv5725/SourceMeasurement.cpp \
	  src/tv5725/InputLine.cpp src/tv5725/Axis.cpp src/tv5725/Scale.cpp \
	  src/tv5725/AxisSolution.cpp src/tv5725/RasterFit.cpp \
	  src/tv5725/PictureOrigin.cpp src/tv5725/BlankingTiming.cpp
	@"$(OUT)/test_source_timing"

## framing-table: host unit tests for Tv5725::FramingTable
#
# The framing a user tuned, kept against the source it was tuned for. Pure, so
# every rule in docs/framing-presets.md that is about the table rather than
# about flash is checked here.
framing-table:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_framing_table" \
	  "$(HERE)/test_framing_table.cpp" src/tv5725/FramingTable.cpp \
	  src/tv5725/SourceKey.cpp src/tv5725/SourceMeasurement.cpp \
	  src/tv5725/InputLine.cpp src/tv5725/PanAndZoom.cpp src/tv5725/Axis.cpp \
	  src/tv5725/Scale.cpp src/tv5725/BlankingTiming.cpp \
	  src/tv5725/AxisSolution.cpp src/tv5725/RasterFit.cpp \
	  src/tv5725/PictureOrigin.cpp
	@"$(OUT)/test_framing_table"

## slot-table: host unit tests for Tv5725::SlotTable
#
# The framing a user stored in a numbered slot, against the source it was stored
# for. Pure, so every rule about the table rather than about flash is checked
# here. docs/framing-presets.md.
slot-table:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_slot_table" \
	  "$(HERE)/test_slot_table.cpp" src/tv5725/SlotTable.cpp \
	  src/tv5725/SourceKey.cpp src/tv5725/SourceMeasurement.cpp \
	  src/tv5725/InputLine.cpp src/tv5725/PanAndZoom.cpp src/tv5725/Axis.cpp \
	  src/tv5725/Scale.cpp src/tv5725/BlankingTiming.cpp \
	  src/tv5725/AxisSolution.cpp src/tv5725/RasterFit.cpp \
	  src/tv5725/PictureOrigin.cpp
	@"$(OUT)/test_slot_table"

## framing-text: host unit tests for Tv5725::FramingText
#
# The framing table as lines of text, and what a partial or hand-edited file
# does. docs/framing-presets.md.
framing-text:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_framing_text" \
	  "$(HERE)/test_framing_text.cpp" src/tv5725/FramingText.cpp \
	  src/tv5725/FramingLine.cpp \
	  src/tv5725/FramingTable.cpp src/tv5725/SourceKey.cpp \
	  src/tv5725/SourceMeasurement.cpp src/tv5725/InputLine.cpp \
	  src/tv5725/PanAndZoom.cpp src/tv5725/Axis.cpp src/tv5725/Scale.cpp \
	  src/tv5725/BlankingTiming.cpp src/tv5725/AxisSolution.cpp \
	  src/tv5725/RasterFit.cpp src/tv5725/PictureOrigin.cpp
	@"$(OUT)/test_framing_text"

## slot-text: host unit tests for Tv5725::SlotText
#
# The slot table as lines of text, and what a partial or hand-edited file does.
# docs/framing-presets.md.
slot-text:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_slot_text" \
	  "$(HERE)/test_slot_text.cpp" src/tv5725/SlotText.cpp \
	  src/tv5725/SlotTable.cpp src/tv5725/FramingLine.cpp \
	  src/tv5725/SourceKey.cpp src/tv5725/SourceMeasurement.cpp \
	  src/tv5725/InputLine.cpp src/tv5725/PanAndZoom.cpp src/tv5725/Axis.cpp \
	  src/tv5725/Scale.cpp src/tv5725/BlankingTiming.cpp \
	  src/tv5725/AxisSolution.cpp src/tv5725/RasterFit.cpp \
	  src/tv5725/PictureOrigin.cpp
	@"$(OUT)/test_slot_text"

## input-line: host unit tests for Tv5725::InputLine
#
# What of a line arrives intact: the hsync pulse at the head, and the write
# limit past which nothing is captured at all. docs/capture-limits.md.
input-line:
	$(call geometry_unit,test_input_line)

## active-image: host unit tests for Tv5725::ActiveImage, and its --dump
#
# The framing is held as state and every window derived from it. capture() and
# clampToLine() recompute the same window and must agree exactly, or a press
# past the edge leaves a dead zone the control never comes back from.
active-image:
	$(call geometry_unit,test_active_image)

## video-processor-timings: host unit tests for Tv5725::VideoProcessorTimings, and its --dump
#
# Both axes from the capture and the raster alone. Nothing is read back off the
# chip -- inheriting a corner or a picture size is what every geometry fault of
# 2026-08-06 was.
video-processor-timings:
	$(call geometry_unit,test_video_processor_timings)

## source-measurement: host unit tests for the ADC sampling divider and the IF line
#
# PLLAD_MD and IF_HSYNC_RST are one quantity in two registers, and every fault
# here has been something moving one without the other. Pure arithmetic, so it
# costs a second to check.
source-measurement:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_source_measurement" \
	  "$(HERE)/test_source_measurement.cpp" src/tv5725/SourceMeasurement.cpp \
	  src/tv5725/InputLine.cpp src/tv5725/Axis.cpp src/tv5725/Scale.cpp \
	  src/tv5725/AxisSolution.cpp src/tv5725/RasterFit.cpp \
	  src/tv5725/PictureOrigin.cpp src/tv5725/BlankingTiming.cpp
	@"$(OUT)/test_source_measurement"

## memory: host unit tests for the playback burst structure
#
# PB_FETCH_NUM sets the memory read request rate, which is one side of the beat
# that tore the picture across 80 of 493 framings. Pure arithmetic.
memory:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_memory" \
	  "$(HERE)/test_memory.cpp" src/tv5725/Memory.cpp
	@"$(OUT)/test_memory"

## sync-output: host unit tests for the encoder's blank across a mode change
#
# The encoder locks to the analog output and keeps the old timing unless sync
# goes away, so the blank is derived from whether a change is outstanding.
sync-output:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_sync_output" \
	  "$(HERE)/test_sync_output.cpp" src/tv5725/SyncOutput.cpp
	@"$(OUT)/test_sync_output"

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
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_display_clock" \
	  "$(HERE)/test_display_clock.cpp" src/tv5725/DisplayClock.cpp \
	  src/clock/ClockGen.cpp src/clock/ClockRamp.cpp
	@"$(OUT)/test_display_clock"

## output-raster: host unit tests for the computed output raster
#
output-raster:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_output_raster" \
	  "$(HERE)/test_output_raster.cpp" src/tv5725/OutputMode.cpp \
	  src/tv5725/OutputTimings.cpp src/tv5725/DisplayClock.cpp \
	  $(CLOCK_SRC)
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

## rate-agreement: host unit tests for when two rate measurements are believed
#
# One timing sample off the debug pin jitters by percent, and both the clock
# generator's seed and FrameSync steer on it. The rule is pure; the measuring
# stays in the sketch.
rate-agreement:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_rate_agreement" \
	  "$(HERE)/test_rate_agreement.cpp" src/clock/RateAgreement.cpp
	@"$(OUT)/test_rate_agreement"

## input-source: host unit tests for the six inputs the OLED offers
#
# The frame byte, the sync routing and what the preferences file keeps, as one
# table. VGA's low nibble is the whole reason it exists: sent as 0x60 the HC32
# leaves HS_IN on sync-on-green and a VGA source never locks.
input-source:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) \
	  -o "$(OUT)/test_input_source" \
	  "$(HERE)/test_input_source.cpp" src/input/InputSource.cpp
	@"$(OUT)/test_input_source"

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

## sdram-timings: host unit tests for how fast the SDRAM bus may run
#
# Derives the memory clock from the EM638325TS-6's tCK, tRCD and tRP, so
# "fastest in spec" is arithmetic against a datasheet rather than a number
# somebody chose.
sdram-timings:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_sdram_timings" \
	  "$(HERE)/test_sdram_timings.cpp" src/tv5725/SdramTimings.cpp
	@"$(OUT)/test_sdram_timings"

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
	  "$(HERE)/test_memory_bus.cpp" src/tv5725/MemoryBus.cpp src/tv5725/SdramTimings.cpp
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
BRINGUP_SRC = $$(ls src/tv5725/*.cpp | grep -vE '^src/tv5725/(Geometry|Controls)\.cpp$$') $(CLOCK_SRC)

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

## hd-bypass: host unit tests for Tv5725::HdBypass
#
# The HD bypass block's resting state -- what the two bypass switches overwrite
# per mode, and the gains, offsets and DVI-mode blanking they never touch. Same
# fake-Wire seam as input-formatter.
hd-bypass:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_hd_bypass" \
	  "$(HERE)/test_hd_bypass.cpp" src/tv5725/HdBypass.cpp
	@"$(OUT)/test_hd_bypass"

## mode-detect: host unit tests for Tv5725::ModeDetect
#
# The thresholds Mode Detect names a standard by. getVideoMode() rewrites eleven
# of them dithered by random(-2,2) off a static captured on its first read, so
# what init() writes is the centre they wander around.
mode-detect:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_mode_detect" \
	  "$(HERE)/test_mode_detect.cpp" src/tv5725/ModeDetect.cpp
	@"$(OUT)/test_mode_detect"

## field-request: host unit tests for FieldRequest
#
# The many-fields-in-one-request wire form. Every register read is queued into
# loop(), so one request per field starves the loop it is measuring.
field-request:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_field_request" \
	  "$(HERE)/test_field_request.cpp" src/net/FieldRequest.cpp
	@"$(OUT)/test_field_request"

## interrupts: host unit tests for Tv5725::Interrupts
#
# The latched "source disturbed" bits, which are the mode-change trigger. They
# do not persist, so reading them is the same act as claiming them.
## sync-processor: host unit tests for SyncProcessor::applyForSyncType
#
# The two copies it replaced sat on different paths, so the check is per field
# and includes the three csync never wrote.
sync-processor:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_sync_processor" \
	  "$(HERE)/test_sync_processor.cpp" src/tv5725/SyncProcessor.cpp
	@"$(OUT)/test_sync_processor"

## video-processor: host unit tests for VideoProcessor::applyPictureOptions
#
# Every register is a BYPS bit, so each is the inverse of the preference naming
# it, and an inverted one leaves a picture merely different rather than wrong.
video-processor:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_video_processor" \
	  "$(HERE)/test_video_processor.cpp" src/tv5725/VideoProcessor.cpp
	@"$(OUT)/test_video_processor"

interrupts:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_interrupts" \
	  "$(HERE)/test_interrupts.cpp" src/tv5725/Interrupts.cpp
	@"$(OUT)/test_interrupts"

## deinterlacer: host unit tests for Tv5725::Deinterlacer
#
# The whole of segment 2, asserted as a byte image against the table it
# replaced: a field nobody thought to list is invisible to per-field checks.
deinterlacer:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_deinterlacer" \
	  "$(HERE)/test_deinterlacer.cpp" src/tv5725/Deinterlacer.cpp
	@"$(OUT)/test_deinterlacer"

## geometry-raster: host unit tests for what the engine does while the source is unsettled
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

## geometry-windows: host unit tests for the registers one solve writes
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

## controls: host unit tests for the OSD and IR control surface
#
# Controls is what the OSD and the remote call. The hardware pad suite reaches
# the engine over HTTP instead, so nothing there can see a press landing on the
# wrong axis.
controls:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_controls" \
	  "$(HERE)/test_controls.cpp" $$(ls src/tv5725/*.cpp) $(CLOCK_SRC)
	@"$(OUT)/test_controls"

## adc: host unit tests for the ADC PLL's divider and latch
#
# PLLAD_LAT loads MD, ND, KS, CKOS and ICP on a rising edge, so only the ORDER
# of the writes distinguishes a loaded divider from one the PLL never saw.
adc:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_adc" \
	  "$(HERE)/test_adc.cpp" src/tv5725/Adc.cpp
	@"$(OUT)/test_adc"

## source-standard: what a video standard implies, and nothing else
#
# Every register here has other writers, so the run is repeated under
# complementary poisons: a field the two disagree about was never written by
# this, whatever it reads.
source-standard:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_source_standard" \
	  "$(HERE)/test_source_standard.cpp" src/tv5725/SourceStandard.cpp src/tv5725/Adc.cpp \
	  src/tv5725/SourceMeasurement.cpp src/tv5725/SyncProcessor.cpp
	@"$(OUT)/test_source_standard"

## sampling-log: host unit tests for Tv5725::SamplingLog
#
# It drives PLLAD_MD itself, so what matters is the ceiling and the restore --
# 3008 took the unit off the network and only a power cycle brought it back.
sampling-log:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_sampling_log" \
	  "$(HERE)/test_sampling_log.cpp" src/tv5725/SamplingLog.cpp src/tv5725/Adc.cpp
	@"$(OUT)/test_sampling_log"

## geometry: the registers each Tv5725::Geometry entry point writes
#
# Each case drives Geometry at the top and reads the chip back through the
# firmware's own register declarations -- InputFormatter::IF_HSYNC_RST and the
# rest -- so a failure names the field and prints both values, and every number
# is checkable against docs/scaler-geometry-model.md. The count of registers
# written is asserted alongside, so a stray write fails too.
geometry:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
	  -o "$(OUT)/test_geometry" \
	  "$(HERE)/test_geometry.cpp" $(GEOMETRY_RASTER_SRC)
	@"$(OUT)/test_geometry"

## register-queue: host unit tests for the deferred register queue
#
# The queue is plain bookkeeping with no Arduino dependency, deliberately: the
# delicate part is a request object that the async side can delete while loop()
# holds it, and that is far cheaper to get right here than on the board.
register-queue:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_register_queue" \
	  "$(HERE)/test_register_queue.cpp" src/net/RegisterQueue.cpp src/net/FieldRequest.cpp
	@"$(OUT)/test_register_queue"

## test: every host unit test
test: axis source-key source-timing framing-table framing-text slot-table slot-text scale sync-output sync-processor video-processor input-line active-image video-processor-timings geometry source-measurement memory display-clock output-raster clock-ramp rate-agreement hold-ramp sync-search input-source segment-select register-queue preset-load memory-map sdram-timings memory-bus bringup frame-buffer input-formatter hd-bypass mode-detect deinterlacer interrupts field-request geometry-raster geometry-windows controls adc source-standard sampling-log
all: test

## clean: remove the compiled test binaries
clean:
	rm -rf "$(OUT)"

