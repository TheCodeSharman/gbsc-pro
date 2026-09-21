// Host-compiled unit tests for Tv5725::SourceKey -- `make -C test source-key`.
//
// What identifies a source, so a framing can be kept against it. The rules it
// has to satisfy are in docs/framing-presets.md, "The key".

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceKey.h"

// The two the sketch supplies. The key only reaches SourceMeasurement for the
// bounds on a count and a rate, so neither is ever called.
#include "DebugPinStub.h"
uint32_t debugPinPulseTicks() { return ticksForHz(0.0f); }
void tv5725Log(const char *) {}

using namespace Tv5725;

TEST_CASE("a source with no measurement behind it identifies nothing")
{
    // Two sources nobody has measured are not the same source, or a framing
    // would carry into whatever arrives next.
    CHECK_FALSE(SourceKey().valid());
    CHECK(SourceKey() != SourceKey());
}

TEST_CASE("the same source measured twice is the same key")
{
    CHECK(SourceKey(311, 50.08f, 0.0f, SourceKey::Negative, SourceKey::Negative) == SourceKey(311, 50.08f, 0.0f, SourceKey::Negative, SourceKey::Negative));
}

// THE COUNT AND THE RATE DO NOT SEPARATE TWO STANDARDS. DMT 640x480@60 and
// CEA 720x480p are both 525 lines at 59.94 Hz, and therefore at the same line
// rate to 0.001%: what differs is the pixel clock, which this chip cannot see,
// and the share of the line its sync takes -- 96/800 against 62/858.
// docs/source-identity-and-framing-lookup.md
TEST_CASE("two standards sharing a count and a rate are told apart by sync width")
{
    CHECK(SourceKey(524, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative)
          != SourceKey(524, 59.94f, 62.0f / 858.0f, SourceKey::Negative, SourceKey::Negative));
}

// The reading dithers by one ADC count while the source stands still: measured
// at 800x600@60, 2499 samples gave 196 and 197 of 1606 and nothing else.
TEST_CASE("the sync width is bucketed wider than it dithers")
{
    CHECK(SourceKey(627, 60.31f, 196.0f / 1606.0f, SourceKey::Negative, SourceKey::Negative)
          == SourceKey(627, 60.31f, 197.0f / 1606.0f, SourceKey::Negative, SourceKey::Negative));
}

// And wider than a sync-type change moves it. The count does not survive that
// change either, which is its own defect, but the width must not be a second
// reason to lose a tuned framing. Measured 0.12204 on separate sync against
// 0.12017 on composite, one mode, nothing else touched.
TEST_CASE("the sync width survives a change of sync type")
{
    CHECK(SourceKey(627, 60.31f, 0.12204f, SourceKey::Negative, SourceKey::Negative) == SourceKey(627, 60.31f, 0.12017f, SourceKey::Negative, SourceKey::Negative));
}

TEST_CASE("the rate is bucketed wider than it jitters")
{
    // The engine re-solves on the measured field rate and that reading wobbles,
    // so an exact match on a float misses its own entry.
    CHECK(SourceKey(311, 50.02f, 0.0f, SourceKey::Negative, SourceKey::Negative) == SourceKey(311, 50.13f, 0.0f, SourceKey::Negative, SourceKey::Negative));
}

TEST_CASE("readings either side of a whole hertz are still the same source")
{
    // The rounding puts these in different hertz -- 60 and 61 -- and the
    // tolerance is what keeps them one source, which is the reason it is wider
    // than the rounding. Measured at 800x600@60: one unchanged source settles
    // at 60.38 Hz after one mode change and 60.72 after the next. Were identity
    // decided by the rounded value, the stored framing would swap with it.
    CHECK(SourceKey(627, 60.38f, 0.0f, SourceKey::Negative, SourceKey::Negative) == SourceKey(627, 60.72f, 0.0f, SourceKey::Negative, SourceKey::Negative));
    CHECK(SourceKey(627, 60.38f, 0.0f, SourceKey::Negative, SourceKey::Negative).rateHz() != SourceKey(627, 60.72f, 0.0f, SourceKey::Negative, SourceKey::Negative).rateHz());
}

TEST_CASE("the key is quantised as finely as the instrument is repeatable")
{
    // The timings are generated FROM the key, so the key is what has to be
    // repeatable -- and repeatable is all it has to be. Rounding to a whole
    // hertz bought that at the cost of the raster: 60.317 stored as 60 is
    // 0.53%, which is 11 px of a 2050 px line, and it went into every absolute
    // geometry measured against that raster.
    //
    // Measured with SamplingLog::rates(), 250 samples a mode: 50.081 Hz and
    // 60.317 Hz, every reading identical. The instrument is repeatable to
    // better than 0.002%, so hundredths cost no repeatability at all.
    // ../docs/investigations/the-rate-tolerance-answered-five-questions.md
    CHECK(SourceKey(311, 50.081f, 0.0f, SourceKey::Negative, SourceKey::Negative).rateHz() == doctest::Approx(50.08f));
    CHECK(SourceKey(627, 60.317f, 0.0f, SourceKey::Negative, SourceKey::Negative).rateHz() == doctest::Approx(60.32f));
    CHECK(SourceKey(524, 59.94f, 0.0f, SourceKey::Negative, SourceKey::Negative).rateHz() == doctest::Approx(59.94f));
}

TEST_CASE("a rate change too small to be movement does not change identity")
{
    // ratesAgree() calls two rates within HeldRateTolerancePerMille the same
    // measurement, so a change inside it arms no mode change. If the key moved
    // there, solveForSource() would swap the stored framing with no re-solve
    // behind it -- a silent reframing on drift.
    //
    // 60.0 against 62.5 is 4.2%, inside the 5% that trigger allows.
    CHECK(SourceKey(627, 60.0f, 0.0f, SourceKey::Negative, SourceKey::Negative) == SourceKey(627, 62.5f, 0.0f, SourceKey::Negative, SourceKey::Negative));
}

TEST_CASE("adjacent standards stay apart")
{
    CHECK(SourceKey(311, 50.08f, 0.0f, SourceKey::Negative, SourceKey::Negative) != SourceKey(311, 60.05f, 0.0f, SourceKey::Negative, SourceKey::Negative));
    CHECK(SourceKey(311, 50.08f, 0.0f, SourceKey::Negative, SourceKey::Negative) != SourceKey(312, 50.08f, 0.0f, SourceKey::Negative, SourceKey::Negative));
}

TEST_CASE("a line count no source runs is not a key")
{
    // A settling source passes through counts inside no standard at all, and a
    // framing stored against one of those is stored against nothing.
    CHECK_FALSE(SourceKey(97, 50.08f, 0.0f, SourceKey::Negative, SourceKey::Negative).valid());
    CHECK_FALSE(SourceKey(311, 0.0f, 0.0f, SourceKey::Negative, SourceKey::Negative).valid());
}

// THE POLARITY PAIR IS HOW THE STANDARDS TELL MODES APART, so both halves of
// it are in the key. DMT publishes 640x480@60 as H-/V- and 800x600@60 as H+/V+.
// docs/source-identity-and-framing-lookup.md
TEST_CASE("two modes sharing a count, a rate and a sync width differ in polarity")
{
    const SourceKey positive(524, 59.94f, 0.12f, SourceKey::Positive, SourceKey::Positive);

    CHECK(positive != SourceKey(524, 59.94f, 0.12f, SourceKey::Negative, SourceKey::Positive));
    CHECK(positive != SourceKey(524, 59.94f, 0.12f, SourceKey::Positive, SourceKey::Negative));
}

// AN ARRANGEMENT THAT CANNOT CARRY A POLARITY RECORDS THAT, rather than a
// plausible 0. Measured on two V+ modes: composite sync reads both polarities 0
// whatever the mode states, because the VIDC20's composite form on the HSync pin
// is a NOR and a NOR is sync-tip-low for both pulses.
TEST_CASE("a polarity that could not be determined is neither of the two")
{
    const SourceKey undetermined(627, 60.32f, 0.12f, SourceKey::Undetermined,
                                 SourceKey::Undetermined);

    CHECK(undetermined != SourceKey(627, 60.32f, 0.12f, SourceKey::Positive,
                                    SourceKey::Positive));
    CHECK(undetermined != SourceKey(627, 60.32f, 0.12f, SourceKey::Negative,
                                    SourceKey::Negative));
}

// A THIRD VALUE RATHER THAN A WILDCARD. One that matched both would make
// equality non-transitive, and a source whose arrangement cannot state its
// polarity still has to find the framing tuned on that same arrangement.
TEST_CASE("two sources on an arrangement that states no polarity are one source")
{
    CHECK(SourceKey(627, 60.32f, 0.12f, SourceKey::Undetermined, SourceKey::Undetermined)
          == SourceKey(627, 60.32f, 0.12f, SourceKey::Undetermined, SourceKey::Undetermined));
}
