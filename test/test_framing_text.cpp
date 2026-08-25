// Host-compiled unit tests for Tv5725::FramingText -- `make -C test framing-text`.
//
// The framing table as lines of text. Pure, so the format's tolerance rules
// from docs/framing-presets.md -- a missing key takes its default, a malformed
// line is skipped rather than fatal -- are checked here rather than against a
// filesystem.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <math.h>
#include <string.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FramingText.h"

float getSourceFieldRate(bool) { return 0.0f; }
void tv5725Log(const char *) {}

using namespace Tv5725;

static const SourceKey Bench(311, 50.08f);

static const char *rendered(const FramingTable &table, uint16_t index, char *buffer)
{
    REQUIRE(FramingText(const_cast<FramingTable &>(table))
                .writeLine(index, buffer, 64));
    return buffer;
}

TEST_CASE("an entry survives being written out and read back")
{
    FramingTable written;
    const PanAndZoom framing(0.0364f, 0.8525f, 0.0740f, 0.8553f);
    REQUIRE(written.remember(Bench, framing));

    char line[64];
    rendered(written, 0, line);

    FramingTable read;
    FramingText(read).readLine(line);

    PanAndZoom back;
    REQUIRE(read.find(Bench, &back));
    CHECK(back.originOn(AxisHorizontal) == doctest::Approx(0.0364f).epsilon(0.001));
    CHECK(back.extentOn(AxisVertical) == doctest::Approx(0.8553f).epsilon(0.001));
}

TEST_CASE("the window in input units is what survives, exactly")
{
    // The requirement is not that the float comes back bit-identical -- it is
    // stored quantised -- but that the WINDOW does: a framing saved and
    // restored unchanged must produce identical registers.
    // docs/framing-presets.md
    for (uint16_t usable = 200; usable <= 1125; usable += 37) {
        for (uint16_t origin = 0; origin + 64 <= usable; origin += 53) {
            const uint16_t extent = (uint16_t)(usable - origin);
            PanAndZoom framing;
            framing.seedOn(AxisHorizontal, (float)origin / usable,
                           (float)extent / usable);
            framing.seedOn(AxisVertical, (float)origin / usable,
                           (float)extent / usable);

            FramingTable written;
            REQUIRE(written.remember(Bench, framing));
            char line[64];
            rendered(written, 0, line);

            FramingTable read;
            FramingText(read).readLine(line);
            PanAndZoom back;
            REQUIRE(read.find(Bench, &back));

            CHECK(lrintf(back.originOn(AxisHorizontal) * usable) == origin);
            CHECK(lrintf(back.extentOn(AxisHorizontal) * usable) == extent);
            CHECK(lrintf(back.originOn(AxisVertical) * usable) == origin);
            CHECK(lrintf(back.extentOn(AxisVertical) * usable) == extent);
        }
    }
}

TEST_CASE("a line that says nothing is skipped rather than fatal")
{
    // A partial file must degrade for the lines it lacks only. Every one of
    // these has been produced by a power cut mid-write or a hand edit.
    const char *ignored[] = {
        "",
        "   ",
        "# the bench RiscPC",
        "311@50",              // no value at all
        "311@50 = 364 8525",   // truncated mid-record
        "@50 = 364 8525 740 8553",
        "311@ = 364 8525 740 8553",
        "nonsense = 1 2 3 4",
        "311@50 = a b c d",
    };
    for (unsigned i = 0; i < sizeof(ignored) / sizeof(*ignored); ++i) {
        FramingTable table;
        FramingText(table).readLine(ignored[i]);
        CHECK_MESSAGE(table.count() == 0, ignored[i]);
    }
}

TEST_CASE("a source the key cannot identify is skipped")
{
    // 97 lines is what a settling source reads, and no standard runs it.
    FramingTable table;
    FramingText(table).readLine("97@50 = 364 8525 740 8553");

    CHECK(table.count() == 0);
}

TEST_CASE("whitespace around the record does not matter")
{
    FramingTable table;
    FramingText(table).readLine("  311@50  =  364   8525  740  8553  ");

    CHECK(table.count() == 1);
    CHECK(table.find(Bench, 0));
}

TEST_CASE("a whole file reads back as the table that wrote it")
{
    FramingTable written;
    REQUIRE(written.remember(SourceKey(311, 50.08f),
                             PanAndZoom(0.03f, 0.85f, 0.07f, 0.85f)));
    REQUIRE(written.remember(SourceKey(525, 59.94f),
                             PanAndZoom(0.05f, 0.90f, 0.03f, 0.94f)));
    REQUIRE(written.remember(SourceKey(628, 60.02f),
                             PanAndZoom(0.11f, 0.70f, 0.09f, 0.80f)));

    FramingTable read;
    char line[64];
    for (uint16_t i = 0; i < written.count(); ++i) {
        rendered(written, i, line);
        FramingText(read).readLine(line);
    }

    CHECK(read.count() == written.count());
    for (uint16_t i = 0; i < written.count(); ++i)
        CHECK(read.find(written.keyAt(i), 0));
}

TEST_CASE("a line for an entry that is not there is not written")
{
    FramingTable table;
    char line[64];

    CHECK_FALSE(FramingText(table).writeLine(0, line, sizeof(line)));
}

TEST_CASE("a buffer too small refuses rather than writing a half record")
{
    // A truncated line read back is a skipped entry, which is a lost tuning
    // reported as nothing at all.
    FramingTable table;
    REQUIRE(table.remember(Bench, PanAndZoom(0.03f, 0.85f, 0.07f, 0.85f)));

    char tiny[8];
    CHECK_FALSE(FramingText(table).writeLine(0, tiny, sizeof(tiny)));
}
