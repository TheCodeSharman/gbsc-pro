// Host-compiled unit tests for Prefs::FramingFile -- `make -C test framing-file`.
//
// The file itself, as against the grammar test_framing_text.cpp covers: what a
// read leaves the table holding, and what a save refuses to put down.
// docs/framing-presets.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string.h>

#include <string>

#include "fake/Wire.h"
#include "fake/LittleFS.h"

FakeTwoWire Wire;
FakeFS LittleFS;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Aspect.h"
#include "../GBSC-Pro-Source code/gbs-control/src/prefs/FramingFile.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FramingText.h"

#include "DebugPinStub.h"
uint32_t debugPinSpanTicks(uint32_t pulses)
{
    return pulses * ticksForHz(0.0f);
}
void tv5725Log(const char *) {}

using namespace Tv5725;

namespace {

const char Path[] = "/framing.txt";

const SourceKey Bench(311, 50.08f, 0.1213f, SourceKey::Negative,
                      SourceKey::Negative);
const SourceKey Wii(524, 59.94f, 0.0733f, SourceKey::Negative,
                    SourceKey::Negative);

struct Stored {
    FramingTable table;
    Prefs::FramingFile file;

    Stored() : file(table, Path) { LittleFS.reset(); }
};

// One source's framing, which is the shortest file a read can take anything
// from.
void seedOneEntry()
{
    FramingTable source;
    source.remember(Bench, PanAndZoom(), Aspect());
    char line[Prefs::FramingFile::LineBytes];
    REQUIRE(FramingText(source).writeLine(0, line, sizeof(line)));
    LittleFS.file[Path] = std::string(line) + "\n";
}

}  // namespace

TEST_CASE("a file that arrives whole is read into the table")
{
    Stored stored;
    seedOneEntry();

    CHECK(stored.file.read());

    CHECK(stored.table.count() == 1);
    CHECK(stored.file.readable());
}

TEST_CASE("nothing stored yet is not a failed read")
{
    // Every source takes its computed default and the first tuning is saveable,
    // so a boot with no file behind it must not refuse to write one.
    Stored stored;

    CHECK(stored.file.read());

    CHECK(stored.table.count() == 0);
    CHECK(stored.file.readable());
}

// THE SIZE IS THE INTEGRITY CHECK, and it costs nothing. A flash awake enough
// to serve metadata and not yet awake enough to serve content satisfies the
// open and the size and hands the parser nothing -- and the format has no
// terminator to notice that with, every line being independently skippable by
// design. Bytes consumed against bytes claimed catches it, and catches a
// partial read too. docs/preferences-file.md
TEST_CASE("a flash serving metadata and not content leaves the table alone")
{
    Stored stored;
    seedOneEntry();
    stored.table.remember(Wii, PanAndZoom(), Aspect());
    LittleFS.serveEmptyForFirst(1);

    CHECK_FALSE(stored.file.read());

    CHECK(stored.table.count() == 1);
    CHECK(stored.table.find(Wii, NULL, NULL));
    CHECK_FALSE(stored.file.readable());
}

TEST_CASE("a save is refused where the read did not come back")
{
    // This is the one way a transient flash fault becomes permanent: the table
    // holds what this boot could read, and writing it puts that over framings
    // still on flash.
    Stored stored;
    seedOneEntry();
    const std::string onFlash = LittleFS.file[Path];
    LittleFS.serveEmptyForFirst(1);
    REQUIRE_FALSE(stored.file.read());

    CHECK_FALSE(stored.file.write());

    CHECK(LittleFS.file[Path] == onFlash);
}

TEST_CASE("what a write puts down is what a read takes back")
{
    Stored stored;
    stored.table.remember(Bench, PanAndZoom(), Aspect());
    stored.table.remember(Wii, PanAndZoom(), Aspect());
    REQUIRE(stored.file.read());
    REQUIRE(stored.file.write());

    FramingTable back;
    Prefs::FramingFile reader(back, Path);
    REQUIRE(reader.read());

    CHECK(back.count() == 2);
    CHECK(back.find(Bench, NULL, NULL));
    CHECK(back.find(Wii, NULL, NULL));
}

TEST_CASE("a written file says what the format is")
{
    Stored stored;
    stored.table.remember(Bench, PanAndZoom(), Aspect());
    REQUIRE(stored.file.read());

    REQUIRE(stored.file.write());

    CHECK(LittleFS.file[Path].find("# framing") == 0);
}

TEST_CASE("a write leaves no tail from a longer file")
{
    // "w" alone has been measured leaving whatever a longer previous file put
    // beyond the new end, and a framing line read out of that tail is a tuning
    // the user never made. ../docs/known-issues.md
    Stored stored;
    stored.table.remember(Bench, PanAndZoom(), Aspect());
    stored.table.remember(Wii, PanAndZoom(), Aspect());
    REQUIRE(stored.file.read());
    REQUIRE(stored.file.write());
    const size_t twoEntries = LittleFS.file[Path].size();

    stored.table.forget(Wii);
    REQUIRE(stored.file.write());

    CHECK(LittleFS.file[Path].size() < twoEntries);
}
