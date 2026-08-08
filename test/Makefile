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

.PHONY: all test geometry hold-ramp clean help
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

## hold-ramp: host unit tests for the IR hold-to-accelerate ramp
hold-ramp:
	@mkdir -p "$(OUT)"
	cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -o "$(OUT)/test_hold_ramp" \
	  "$(HERE)/test_hold_ramp.cpp" src/input/HoldRamp.cpp
	@"$(OUT)/test_hold_ramp"

## test: every host unit test
test: geometry hold-ramp
all: test

## clean: remove the compiled test binaries
clean:
	rm -rf "$(OUT)"
