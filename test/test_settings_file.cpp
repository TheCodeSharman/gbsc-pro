// Host-compiled unit tests for Prefs::SettingsFile -- `make -C test settings-file`.
//
// The file itself, as against the grammar test_settings.cpp covers: how many
// attempts a read spends, what it starts each one from, and what it refuses to
// write. docs/preferences-file.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string.h>

#include <string>
#include <vector>

#include "fake/Wire.h"
#include "fake/LittleFS.h"

FakeTwoWire Wire;
FakeFS LittleFS;

#include "../GBSC-Pro-Source code/gbs-control/options.h"
#include "../GBSC-Pro-Source code/gbs-control/src/audio/LineVolume.h"
#include "../GBSC-Pro-Source code/gbs-control/src/prefs/Settings.h"
#include "../GBSC-Pro-Source code/gbs-control/src/prefs/SettingsFile.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourBalance.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PictureOptions.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceSelection.h"

void tv5725Log(const char *) {}

namespace {

const char Path[] = "/preferences.txt";

// Everything a settings file carries, the grammar over it, and the file.
struct Stored {
    userOptions options;
    avOptions av;
    Tv5725::ColourBalance colour;
    Tv5725::PictureOptions picture;
    uint8_t volume;
    uint8_t legacyInput;
    uint8_t brightnessSet;
    Prefs::Settings settings;
    Prefs::SettingsFile file;

    Stored()
        : volume(0), legacyInput(0), brightnessSet(0),
          settings(options, av, colour, picture, volume, legacyInput,
                   brightnessSet),
          file(settings, Path)
    {
        LittleFS.reset();
        VideoSourceSelection::forgetSelection();
        settings.defaults();
    }
};

// A file holding one setting and the terminator, which is the shortest thing a
// read can succeed on.
std::string fileHolding(const char *line)
{
    return std::string("# GBSC-Pro settings\n") + line + "\n"
           + Prefs::Settings::terminator() + "\n";
}

}  // namespace

TEST_CASE("a file reaching its terminator is read")
{
    Stored unit;
    LittleFS.file[Path] = fileHolding("output = 1280x720");

    CHECK(unit.file.read(1));
    CHECK(std::string(unit.options.outputResolution) == "1280x720");
}

// **A READ THAT STOPS SHORT IS A FAILED READ, NOT A SHORT ONE.** The terminator
// is the whole of the integrity check the format needs, and it works at any
// length where a byte count only worked while every build wrote the same number
// of them.
TEST_CASE("a file that never reaches its terminator is refused")
{
    Stored unit;
    LittleFS.file[Path] = "# GBSC-Pro settings\noutput = 1280x720\n";

    CHECK_FALSE(unit.file.read(1));
}

// **WAIT FOR THE FILE TO BE READABLE, NOT MERELY OPENABLE.** A flash awake
// enough to serve metadata and not yet awake enough to serve content satisfies
// the open and the size on the first attempt and hands the parser nothing, so
// the retry is on reaching the terminator rather than on the open.
TEST_CASE("a flash still waking up is retried until the content arrives")
{
    Stored unit;
    LittleFS.file[Path] = fileHolding("output = 1280x720");
    LittleFS.serveEmptyForFirst(3);

    CHECK(unit.file.read(10));
    CHECK(std::string(unit.options.outputResolution) == "1280x720");
}

TEST_CASE("a flash that never wakes up is given up on after the attempts")
{
    Stored unit;
    LittleFS.file[Path] = fileHolding("output = 1280x720");
    LittleFS.serveEmptyForFirst(20);

    CHECK_FALSE(unit.file.read(4));
    CHECK(LittleFS.opens() == 4);
}

// Every attempt starts from the defaults, so a part-applied read is never what
// the next one builds on -- a file whose first lines arrive and whose last do
// not would otherwise leave half of one read under the next.
TEST_CASE("each attempt starts from the defaults")
{
    Stored unit;
    LittleFS.file[Path] = fileHolding("output = 1280x720");
    LittleFS.serveEmptyForFirst(1);
    strncpy(unit.options.outputResolution, "640x480",
            sizeof(unit.options.outputResolution) - 1);

    REQUIRE(unit.file.read(4));

    CHECK(std::string(unit.options.outputResolution) == "1280x720");
}

TEST_CASE("a missing file is not a read that can be retried into existence")
{
    Stored unit;

    CHECK_FALSE(unit.file.read(3));
    CHECK_FALSE(unit.file.exists());
}

// The round trip is what says a save is readable by the next boot, which is the
// only thing a save is for.
TEST_CASE("what a write puts down is what a read takes back")
{
    Stored unit;
    strncpy(unit.options.outputResolution, "1280x720",
            sizeof(unit.options.outputResolution) - 1);
    unit.options.enableFrameTimeLock = 1;

    REQUIRE(unit.file.write());
    const std::string written = LittleFS.file[Path];

    // Stored's constructor resets the fake, so what was written is carried
    // across rather than read through the instance that wrote it.
    Stored next;
    LittleFS.file[Path] = written;
    REQUIRE(next.file.read(1));
    CHECK(std::string(next.options.outputResolution) == "1280x720");
    CHECK(next.options.enableFrameTimeLock == 1);
}

// **A SHORTER WRITE OVER A LONGER FILE LEAVES A TAIL.** Opening "w" alone has
// been measured leaving whatever wrote the file longer behind the terminator.
// The terminator makes one inert to a read, but a settings file is read by
// people too. docs/known-issues.md
TEST_CASE("a write leaves no tail from a longer file")
{
    Stored unit;
    LittleFS.file[Path] = std::string(4000, 'Q');

    REQUIRE(unit.file.write());

    CHECK(LittleFS.file[Path].find('Q') == std::string::npos);
}

TEST_CASE("a written file says what the format is")
{
    Stored unit;

    REQUIRE(unit.file.write());

    CHECK(LittleFS.file[Path].find("docs/preferences-file.md")
          != std::string::npos);
}

// Reported per attempt so the caller can log it without this class needing a
// clock or a printf -- the boot log's PREFS lines are what tell a file holding
// defaults from a read that failed.
TEST_CASE("each attempt is reported to the caller")
{
    static std::vector<Prefs::SettingsFile::Attempt> seen;
    seen.clear();

    Stored unit;
    LittleFS.file[Path] = fileHolding("output = 1280x720");
    LittleFS.serveEmptyForFirst(2);

    REQUIRE(unit.file.read(10, [](const Prefs::SettingsFile::Attempt &a) {
        seen.push_back(a);
    }));

    REQUIRE(seen.size() == 3);
    CHECK_FALSE(seen[0].reachedEnd);
    CHECK(seen[0].applied == 0);
    CHECK(seen[2].reachedEnd);
    CHECK(seen[2].applied == 1);
}
