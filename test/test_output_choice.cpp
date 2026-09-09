// Host-compiled unit tests for src/tv5725/OutputChoice.h -- `make -C test output-choice`.
//
// The user's output preference, and the mode it names. A preference is one
// resolution whatever the source runs at: the field-rate swap that used to sit
// here keyed the output's height on the source's RATE, which stands in for a
// line count only on broadcast sources.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "Si5351Stubs.h"

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputChoice.h"

using namespace Tv5725;

TEST_CASE("a preference is the resolution it names, and nothing qualifies it")
{
    const PresetPreference asked[] = { Output480P, Output576P, Output720P,
                                       Output960P, Output1024P, Output1080P };
    for (unsigned i = 0; i < sizeof(asked) / sizeof(asked[0]); ++i) {
        const OutputChoice choice(asked[i]);
        CHECK((choice.resolve() == OutputMode::forPreference(asked[i])));
    }
}

TEST_CASE("the two members of each old pair are reachable on their own")
{
    // 480p against 576p and 960p against 1024p were swapped between by field
    // rate. Each is a preference in its own right, so either is selectable.
    CHECK((OutputChoice(Output480P).resolve() == &Mode480p));
    CHECK((OutputChoice(Output576P).resolve() == &Mode576p));
    CHECK((OutputChoice(Output960P).resolve() == &Mode960p));
    CHECK((OutputChoice(Output1024P).resolve() == &Mode1024p));
}

TEST_CASE("bypass is an output mode, not the absence of one")
{
    // A resolution choice whose resolution is the source's. It resolves to a
    // mode like any other, so "what is the output doing" has one answer and
    // nothing has to read a null pointer as an answer.
    CHECK((OutputChoice(OutputBypass).resolve() == &ModeBypass));
    CHECK(ModeBypass.isBypass());
}

TEST_CASE("no real resolution is bypass")
{
    const OutputMode *real[] = { &Mode1080p, &Mode1024p, &Mode960p,
                                 &Mode720p, &Mode576p, &Mode480p };
    for (unsigned i = 0; i < sizeof(real) / sizeof(real[0]); ++i)
        CHECK_FALSE(real[i]->isBypass());
}

TEST_CASE("a choice that names no resolution resolves to no mode")
{
    // Distinct from bypass, which names one. A custom preset's saved bytes are
    // the mode, and a default-constructed choice is what a caller with none
    // hands over -- neither is a resolution this can name.
    CHECK((OutputChoice().resolve() == 0));
    CHECK((OutputChoice(OutputCustomized).resolve() == 0));
    CHECK((OutputChoice((PresetPreference)6).resolve() == 0));
}

// A scaling RGBHV load has to name a resolution. The preference may not -- it
// can be pass-through, which is not a resolution to scale to -- so the load
// asks for the scaled reading of it rather than each caller carrying a literal.

TEST_CASE("a preference that names a resolution is its own scaled reading")
{
    const PresetPreference asked[] = { Output480P, Output576P, Output720P,
                                       Output960P, Output1024P, Output1080P };
    for (unsigned i = 0; i < sizeof(asked) / sizeof(asked[0]); ++i)
        CHECK((OutputChoice::scaledOr(asked[i]) == asked[i]));
}

TEST_CASE("a preference naming no resolution reads as the scaled default")
{
    CHECK((OutputChoice::scaledOr(OutputBypass) == OutputChoice::ScaledDefault));
    CHECK((OutputChoice::scaledOr(OutputCustomized) == OutputChoice::ScaledDefault));
}

TEST_CASE("the scaled default resolves to a mode")
{
    CHECK((OutputChoice(OutputChoice::ScaledDefault).resolve() != 0));
}
