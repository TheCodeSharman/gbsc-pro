// Host-compiled unit tests for src/tv5725/OutputChoice.h -- `make -C test output-choice`.
//
// The user's output preference, and the two things that qualify it, resolved
// against a field rate the engine has to measure first. The eight-cell table
// below is the bench oracle: the same eight questions asked of the unit over
// /uc? and /sc?Z, at 50 Hz and 60 Hz, matchPresetSource on and off.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "Si5351Stubs.h"

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputChoice.h"

using namespace Tv5725;

static const bool Matching = true;
static const bool NoMatching = false;
static const bool DownshiftAllowed = true;
static const bool NotForced = false;

TEST_CASE("matchPresetSource swaps the SD pair to whichever member the source runs at")
{
    // The bench oracle, captured 2026-08-27 by driving the source between
    // 320x256@50 and 640x480@60 and asking for each resolution over /uc?.
    // Every cell is what a rate-keyed resolution gives, in both directions.
    struct Cell { float rate; bool match; PresetPreference asked; const OutputMode *got; };
    const Cell oracle[] = {
        { 50.0f, Matching,   Output480P, &Mode576p },
        { 50.0f, Matching,   Output576P, &Mode576p },
        { 50.0f, NoMatching, Output480P, &Mode480p },
        { 50.0f, NoMatching, Output576P, &Mode576p },
        { 60.0f, Matching,   Output480P, &Mode480p },
        { 60.0f, Matching,   Output576P, &Mode480p },
        { 60.0f, NoMatching, Output480P, &Mode480p },
        { 60.0f, NoMatching, Output576P, &Mode576p },
    };

    for (unsigned i = 0; i < sizeof(oracle) / sizeof(oracle[0]); ++i) {
        const OutputChoice choice(oracle[i].asked, oracle[i].match,
                                  DownshiftAllowed, NotForced);
        CHECK((choice.resolve(oracle[i].rate) == oracle[i].got));
    }
}

TEST_CASE("the swap keys on the measured rate, not on a nominal one")
{
    // 50.02 Hz is a normal reading on the bench and 59.94 is the NTSC rate, so
    // the split has to be a threshold rather than an equality.
    const OutputChoice choice(Output480P, Matching, DownshiftAllowed, NotForced);
    CHECK((choice.resolve(50.02f) == &Mode576p));
    CHECK((choice.resolve(59.94f) == &Mode480p));
}

TEST_CASE("matchPresetSource swaps 960p up to 1024p on a 50 Hz source")
{
    const OutputChoice choice(Output960P, Matching, DownshiftAllowed, NotForced);
    CHECK((choice.resolve(50.0f) == &Mode1024p));
    CHECK((choice.resolve(60.0f) == &Mode960p));
}

TEST_CASE("the 1024p downshift is refused for a source that is not to be downshifted")
{
    // The asymmetry is upstream's rather than a design: the PAL side swaps
    // unguarded while the NTSC side excludes standard 8 and a scaling-RGBHV
    // source. It is preserved verbatim, and it is the one cell this bench
    // cannot reach -- an RGBHV source under 535 lines resolves the guard to
    // false at both rates.
    const OutputChoice guarded(Output1024P, Matching, !DownshiftAllowed, NotForced);
    CHECK((guarded.resolve(60.0f) == &Mode1024p));

    const OutputChoice open(Output1024P, Matching, DownshiftAllowed, NotForced);
    CHECK((open.resolve(60.0f) == &Mode960p));
}

TEST_CASE("the SD pair carries no such guard")
{
    // It was inside the preference itself upstream, which made 480p unaskable
    // on a 50 Hz source. Here it is a convenience, and symmetric.
    const OutputChoice guarded(Output576P, Matching, !DownshiftAllowed, NotForced);
    CHECK((guarded.resolve(60.0f) == &Mode480p));
}

TEST_CASE("a source shown at 60 keys the swaps to 60, whatever it was measured at")
{
    // PalForce60 shows a 50 Hz source at 60. The output is what the pair swaps
    // are about, so the measured rate is not what they key on here.
    const OutputChoice forced(Output480P, Matching, DownshiftAllowed, true);
    CHECK((forced.resolve(50.0f) == &Mode480p));

    const OutputChoice notForced(Output480P, Matching, DownshiftAllowed, NotForced);
    CHECK((notForced.resolve(50.0f) == &Mode576p));
}

TEST_CASE("without matchPresetSource a preference is the resolution, at either rate")
{
    const PresetPreference asked[] = { Output480P, Output576P, Output720P,
                                       Output960P, Output1024P, Output1080P };
    for (unsigned i = 0; i < sizeof(asked) / sizeof(asked[0]); ++i) {
        const OutputChoice choice(asked[i], NoMatching, DownshiftAllowed, NotForced);
        CHECK((choice.resolve(50.0f) == choice.resolve(60.0f)));
        CHECK((choice.resolve(50.0f) == OutputMode::forPreference(asked[i])));
    }
}

TEST_CASE("a choice that names no resolution resolves to no mode")
{
    // Bypass and a custom preset both keep whatever raster they had. A
    // default-constructed choice is the same answer for a caller that has none.
    CHECK((OutputChoice().resolve(50.0f) == 0));
    CHECK((OutputChoice(OutputBypass).resolve(50.0f) == 0));
    CHECK((OutputChoice(OutputCustomized).resolve(50.0f) == 0));
}

TEST_CASE("a choice of one resolution matches nothing and is that resolution")
{
    // What the direct commands ask for: this resolution, whatever the source
    // runs at and whatever the user's preference says.
    CHECK((OutputChoice(Output1080P).resolve(50.0f) == &Mode1080p));
    CHECK((OutputChoice(Output480P).resolve(50.0f) == &Mode480p));
    CHECK((OutputChoice(Output960P).resolve(50.0f) == &Mode960p));
}
