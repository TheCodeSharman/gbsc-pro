// Host-compiled unit tests for src/tv5725/PresetLoad.h -- `make -C test preset-load`.
//
// writeProgramArrayNew() does two unrelated jobs: it writes 432 bytes from a
// preset table, and it decides mode state that has nothing to do with the table.
// The second has to outlive the first, or it goes out with the tables.
//
// So the decisions live here as arithmetic over plain integers, with the sketch
// keeping the register and rto-> traffic.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PresetLoad.h"

using Tv5725::PresetLoad;

// ADC_INPUT_SEL is the TV5725's own input mux, and 0 selects the YPbPr pins.
// Note this is only half the input path -- whether the HC32F460 has actually
// connected anything to it is ASW_01..04, which no register dump can see.
static const uint8_t AdcYpbpr = 0;
static const uint8_t AdcRgb = 1;

TEST_CASE("videoStandardInput 15 is normalised to 0 before the table is written")
{
    // 15 is the "no valid mode" sentinel. The byte loop READS this value while
    // it runs -- the table's byte at index 375 is chosen by
    // videoStandardInputIsPalNtscSd() -- so the normalisation has to be settled
    // before the first byte goes out, not after.
    PresetLoad load(15, AdcRgb, false, false);

    CHECK(load.videoStandardInput() == 0);
}

TEST_CASE("every other videoStandardInput reaches the table unchanged")
{
    // Only 15 is a sentinel; 1..4 are the real standards and 0 is already
    // "none". Rewriting any of those would change which byte index 375 gets.
    for (uint8_t standard = 0; standard <= 4; standard++) {
        PresetLoad load(standard, AdcRgb, false, false);
        CHECK(load.videoStandardInput() == standard);
    }
}

TEST_CASE("the input is YPbPr exactly when the ADC mux is on input 0")
{
    SUBCASE("mux 0 is YPbPr") {
        PresetLoad load(2, AdcYpbpr, false, false);
        CHECK(load.inputIsYpBpR() == true);
    }

    SUBCASE("any other mux setting is not") {
        PresetLoad load(2, AdcRgb, false, false);
        CHECK(load.inputIsYpBpR() == false);
    }
}

TEST_CASE("scaling RGBHV needs both the preference and a source that can take it")
{
    // Wanting it is not enough: an RGBHV source over 535 lines is trapped in
    // bypass and is never scaled, which is what isValidForScalingRGBHV carries.
    SUBCASE("preferred and valid") {
        PresetLoad load(0, AdcRgb, true, true);
        CHECK(load.enableScalingRgbhv() == true);
    }

    SUBCASE("preferred but not valid") {
        PresetLoad load(0, AdcRgb, true, false);
        CHECK(load.enableScalingRgbhv() == false);
    }

    SUBCASE("valid but not preferred") {
        PresetLoad load(0, AdcRgb, false, true);
        CHECK(load.enableScalingRgbhv() == false);
    }
}

TEST_CASE("enabling scaling RGBHV moves videoStandardInput to 3 after the load")
{
    // 3 is the scaling-RGBHV standard. This happens AFTER the table is written,
    // so the two values genuinely differ during one load and the class has to
    // report both -- collapsing them to one accessor would write 3 into the
    // value the byte loop reads.
    PresetLoad load(0, AdcRgb, true, true);

    CHECK(load.videoStandardInput() == 0);
    CHECK(load.videoStandardInputAfterLoad() == 3);
}

TEST_CASE("without scaling RGBHV the standard is left as the table saw it")
{
    PresetLoad load(2, AdcRgb, true, false);

    CHECK(load.videoStandardInputAfterLoad() == 2);
}

TEST_CASE("the sentinel normalisation survives to the end of the load")
{
    // 15 -> 0 before, and nothing puts it back: a load that does not turn on
    // scaling RGBHV must still leave 0 behind, or the next detection sees the
    // sentinel it was there to clear.
    PresetLoad load(15, AdcRgb, false, false);

    CHECK(load.videoStandardInputAfterLoad() == 0);
}

TEST_CASE("a scaled RGBHV source changes preset when its line count changes bucket")
{
    // The preset a scaled RGBHV source runs is chosen by its line count, and it
    // is reloaded when the count crosses 280 or 380 away from the count the
    // loaded preset was chosen for. 0 means keep the one already loaded.
    SUBCASE("no crossing keeps the loaded preset") {
        PresetLoad::rememberScalingRgbhv(311);
        CHECK(PresetLoad::rgbhvPresetStandard(311) == 0);
        CHECK(PresetLoad::rgbhvPresetStandard(300) == 0);

        PresetLoad::rememberScalingRgbhv(525);
        CHECK(PresetLoad::rgbhvPresetStandard(525) == 0);
    }

    SUBCASE("down past 280") {
        PresetLoad::rememberScalingRgbhv(311);
        CHECK(PresetLoad::rgbhvPresetStandard(262) == 1);
    }

    SUBCASE("down past 380 but not past 280") {
        PresetLoad::rememberScalingRgbhv(525);
        CHECK(PresetLoad::rgbhvPresetStandard(312) == 2);
    }

    SUBCASE("up past 380") {
        PresetLoad::rememberScalingRgbhv(311);
        CHECK(PresetLoad::rgbhvPresetStandard(525) == 3);
    }

    SUBCASE("a load that did not establish its source is not a crossing") {
        PresetLoad::rememberScalingRgbhv(PresetLoad::SourceLinesUnknown);
        CHECK(PresetLoad::rgbhvPresetStandard(311) == 0);
    }
}

// Which preset a newly measured scaling-RGBHV source wants. The buckets are
// upstream's, and they are a MEASUREMENT -- a line count and a field rate --
// wearing a standard byte's clothes.

TEST_CASE("a source under 280 lines takes the first preset")
{
    CHECK(PresetLoad::rgbhvStandardFor(262, 60.0f) == 1);
    CHECK(PresetLoad::rgbhvStandardFor(279, 50.0f) == 1);
}

TEST_CASE("a source under 380 lines takes the second, whatever its rate")
{
    // The bench RISC PC at 320x256 is 311 lines, and lands here at 50 Hz and
    // at 60 Hz alike -- the rate is not consulted below 380.
    CHECK(PresetLoad::rgbhvStandardFor(311, 50.08f) == 2);
    CHECK(PresetLoad::rgbhvStandardFor(311, 60.0f) == 2);
}

TEST_CASE("a tall source at a 50 Hz-ish rate takes the fourth")
{
    CHECK(PresetLoad::rgbhvStandardFor(627, 50.0f) == 4);
    CHECK(PresetLoad::rgbhvStandardFor(627, 44.1f) == 4);
    CHECK(PresetLoad::rgbhvStandardFor(627, 53.7f) == 4);
}

TEST_CASE("a tall source outside that rate window takes the third")
{
    CHECK(PresetLoad::rgbhvStandardFor(627, 60.0f) == 3);
    CHECK(PresetLoad::rgbhvStandardFor(627, 44.0f) == 3);
    CHECK(PresetLoad::rgbhvStandardFor(627, 53.8f) == 3);
}

// Whether the output in force is scaling RGBHV. State rather than a chip
// register: the firmware kept it in an address the datasheet does not document,
// where a load cleared it and every reader had to be ordered around that.

TEST_CASE("nothing is scaling RGBHV until a load says so")
{
    PresetLoad::forgetScalingRgbhv();

    CHECK_FALSE(PresetLoad::scalingRgbhvInForce());
}

TEST_CASE("a load that enables scaling RGBHV is remembered")
{
    PresetLoad::forgetScalingRgbhv();

    PresetLoad::rememberScalingRgbhv(true);

    CHECK(PresetLoad::scalingRgbhvInForce());
}

TEST_CASE("the next load forgets what the last one enabled")
{
    PresetLoad::forgetScalingRgbhv();
    PresetLoad::rememberScalingRgbhv(311);

    PresetLoad::forgetScalingRgbhv();

    CHECK_FALSE(PresetLoad::scalingRgbhvInForce());
}

// The count the loaded preset was chosen for travels with the flag saying one
// is loaded: they are one fact, and a reader comparing a fresh count against a
// stale one reloads a preset the source never left.

TEST_CASE("the count a scaling RGBHV preset was loaded for is held with it")
{
    PresetLoad::forgetScalingRgbhv();

    PresetLoad::rememberScalingRgbhv(311);

    CHECK(PresetLoad::scalingRgbhvInForce());
    CHECK(PresetLoad::rgbhvPresetStandard(311) == 0);
    CHECK(PresetLoad::rgbhvPresetStandard(525) == 3);
}

TEST_CASE("forgetting the load forgets the count it was chosen for")
{
    PresetLoad::rememberScalingRgbhv(525);

    PresetLoad::forgetScalingRgbhv();

    // Nothing is loaded, so no count can have crossed a bucket away from it.
    CHECK(PresetLoad::rgbhvPresetStandard(262) == 0);
    CHECK(PresetLoad::rgbhvPresetStandard(311) == 0);
}
