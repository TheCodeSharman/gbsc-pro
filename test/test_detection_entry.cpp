// Host tests for DetectionEntry -- `make -C test detection-entry`.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/DetectionEntry.h"

TEST_CASE("a signal arriving is acted on at once")
{
    CHECK(DetectionEntry::stepAt(true, 0) == DetectionEntry::Act);
    CHECK(DetectionEntry::stepAt(true, DetectionEntry::WindowMs - 1)
          == DetectionEntry::Act);
}

TEST_CASE("nothing arriving yet is waited out rather than concluded on")
{
    // KEEP LOOKING FOR THE WHOLE WINDOW. A mux that has just moved has not
    // delivered a line yet, and concluding on the first sample declares a
    // present source absent -- which sends the caller to
    // goLowPowerWithInputDetection(), zeroing segments 0 and 2 on a source that
    // was there all along.
    CHECK(DetectionEntry::stepAt(false, 0) == DetectionEntry::Wait);
    CHECK(DetectionEntry::stepAt(false, DetectionEntry::WindowMs / 2)
          == DetectionEntry::Wait);
    CHECK(DetectionEntry::stepAt(false, DetectionEntry::WindowMs - 1)
          == DetectionEntry::Wait);
}

TEST_CASE("the window is spent at its own length, not one sample past it")
{
    CHECK(DetectionEntry::stepAt(false, DetectionEntry::WindowMs)
          == DetectionEntry::GiveUp);
    CHECK(DetectionEntry::stepAt(false, DetectionEntry::WindowMs + 1000)
          == DetectionEntry::GiveUp);
}

TEST_CASE("a signal outlasts a spent window")
{
    // The window bounds how long absence is waited out. It does not bound how
    // long a source is allowed to be acted on for, and a reading taken as the
    // window closes is still a reading.
    CHECK(DetectionEntry::stepAt(true, DetectionEntry::WindowMs)
          == DetectionEntry::Act);
}
