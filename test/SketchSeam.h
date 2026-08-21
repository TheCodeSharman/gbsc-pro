#ifndef TEST_SKETCH_SEAM_H_
#define TEST_SKETCH_SEAM_H_

// The free functions src/tv5725/ declares and the sketch defines. Any host
// binary linking SourceMeasurement.cpp needs both, and a suite that neither
// measures nor reads the log wants nothing more than this.
//
// Header-only and defining its globals, the way BenchGeometry.h does: every host
// test is a single-translation-unit binary, so one include per binary is the
// whole contract.
//
// A suite that DRIVES the field rate or asserts the diagnostic defines its own
// instead of including this -- test_source_measurement.cpp does both.

#include <Arduino.h>

#include "Si5351Stubs.h"

float getSourceFieldRate(boolean) { return 50.08f; }
void tv5725Log(const char *) {}


#endif  // TEST_SKETCH_SEAM_H_
