// Host-compiled unit tests for Prefs::Settings -- `make -C test settings`.
//
// The settings file as lines of text. The tolerance rules are what the
// positional file it replaces could not have: a missing key takes its default,
// an unknown one is skipped, and a read that stops short leaves the settings it
// did not reach alone. docs/preferences-file.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string.h>

#include <string>
#include <vector>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/options.h"
#include "../GBSC-Pro-Source code/gbs-control/src/prefs/Settings.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourBalance.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceSelection.h"

void tv5725Log(const char *) {}

// Everything a settings file carries, and the file itself.
struct Stored {
    userOptions options;
    avOptions av;
    Tv5725::ColourBalance colour;
    uint8_t volume;
    uint8_t legacyInput;
    uint8_t brightnessSet;
    Prefs::Settings settings;

    Stored()
        : volume(0), legacyInput(0), brightnessSet(0),
          settings(options, av, colour, volume, legacyInput, brightnessSet)
    {
        VideoSourceSelection::forgetSelection();
        settings.defaults();
    }

    std::vector<std::string> lines()
    {
        std::vector<std::string> written;
        char line[80];
        for (uint16_t i = 0; settings.writeLine(i, line, sizeof(line)); ++i)
            written.push_back(line);
        return written;
    }

    std::string saved()
    {
        std::string file;
        const std::vector<std::string> written = lines();
        for (size_t i = 0; i < written.size(); ++i)
            file += written[i] + "\n";
        return file + Prefs::Settings::terminator() + "\n";
    }

    // Every line of a file, as a boot reads it.
    void load(const std::string &file)
    {
        std::string line;
        for (size_t i = 0; i <= file.size(); ++i) {
            if (i == file.size() || file[i] == '\n') {
                settings.readLine(line.c_str());
                line.clear();
            } else {
                line += file[i];
            }
        }
    }
};

TEST_CASE("a saved file read back gives the settings it was written from")
{
    Stored written;
    written.options.enableFrameTimeLock = 1;
    written.options.scanlineStrength = 0x44;
    written.options.presetSlot = 'C';
    strcpy(written.options.outputResolution, "1280x720");
    written.volume = 23;
    written.av.contrast = 200;
    written.colour.adopt(100, 110, 120, 130);

    Stored read;
    read.load(written.saved());

    CHECK(read.options.enableFrameTimeLock == 1);
    CHECK(read.options.scanlineStrength == 0x44);
    CHECK(read.options.presetSlot == 'C');
    CHECK(std::string(read.options.outputResolution) == "1280x720");
    CHECK(read.volume == 23);
    CHECK(read.av.contrast == 200);
    CHECK(read.colour.red() == 100);
    CHECK(read.colour.lumaGain() == 130);
}

TEST_CASE("a missing key takes its default and leaves every other alone")
{
    Stored read;
    read.options.wantPeaking = 0;
    read.options.enableFrameTimeLock = 1;

    read.settings.defaults();
    read.load("frame-time-lock = 1\n");

    CHECK(read.options.enableFrameTimeLock == 1);
    CHECK(read.options.wantPeaking == 1);
}

TEST_CASE("an unknown key is skipped and the settings around it still apply")
{
    Stored read;
    read.load("frame-time-lock = 1\nwant-flying-cars = 9\nvolume = 7\n");

    CHECK(read.options.enableFrameTimeLock == 1);
    CHECK(read.volume == 7);
}

TEST_CASE("a malformed line is skipped rather than fatal")
{
    Stored read;
    read.load("frame-time-lock\n= 4\n\nvolume = 7\n");

    CHECK(read.options.enableFrameTimeLock == 0);
    CHECK(read.volume == 7);
}

TEST_CASE("a comment and a blank line are skipped")
{
    Stored read;
    CHECK(read.settings.readLine("# volume = 50") == Prefs::Settings::Skipped);
    CHECK(read.settings.readLine("") == Prefs::Settings::Skipped);
    CHECK(read.settings.readLine("   ") == Prefs::Settings::Skipped);
    CHECK(read.volume == 0);
}

TEST_CASE("the terminator is reported, so a read that stops short is visible")
{
    Stored read;
    CHECK(read.settings.readLine(Prefs::Settings::terminator()) ==
          Prefs::Settings::End);
    CHECK(read.settings.readLine("volume = 7") == Prefs::Settings::Applied);
}

TEST_CASE("a save ends with the terminator")
{
    Stored written;
    const std::string file = written.saved();
    const std::string tail =
        std::string(Prefs::Settings::terminator()) + "\n";
    CHECK(file.size() > tail.size());
    CHECK(file.compare(file.size() - tail.size(), tail.size(), tail) == 0);
}

TEST_CASE("a file truncated mid-line leaves what it did not reach at defaults")
{
    Stored written;
    written.options.enableFrameTimeLock = 1;
    written.volume = 31;
    const std::string whole = written.saved();

    Stored read;
    read.load(whole.substr(0, whole.find("volume")));

    CHECK(read.options.enableFrameTimeLock == 1);
    CHECK(read.volume == 0);
}

TEST_CASE("a value past its bound reads as the default")
{
    Stored read;
    read.load("deinterlace-mode = 9\nvolume = 200\npeaking = 4\n");

    CHECK(read.options.deintMode == 0);
    CHECK(read.volume == 0);
    CHECK(read.options.wantPeaking == 1);
}

TEST_CASE("a value that is not a number reads as the default")
{
    Stored read;
    read.load("volume = loud\nscanline-strength =\n");

    CHECK(read.volume == 0);
    CHECK(read.options.scanlineStrength == 0x30);
}

TEST_CASE("a hand-written leading zero is still a number")
{
    Stored read;
    read.load("volume = 007\n");

    CHECK(read.volume == 7);
}

TEST_CASE("space around the separator is optional")
{
    Stored read;
    read.load("volume=7\n   frame-time-lock   =   1   \n");

    CHECK(read.volume == 7);
    CHECK(read.options.enableFrameTimeLock == 1);
}

TEST_CASE("every setting is written once, under its own key")
{
    Stored written;
    const std::vector<std::string> lines = written.lines();
    REQUIRE(lines.size() > 20);

    std::vector<std::string> keys;
    for (size_t i = 0; i < lines.size(); ++i) {
        const size_t at = lines[i].find(" = ");
        REQUIRE(at != std::string::npos);
        keys.push_back(lines[i].substr(0, at));
    }

    for (size_t i = 0; i < keys.size(); ++i)
        for (size_t j = i + 1; j < keys.size(); ++j)
            CHECK_MESSAGE(keys[i] != keys[j], "duplicate key " << keys[i]);
}

TEST_CASE("a line that will not fit is not written at all")
{
    Stored written;
    char line[8];
    CHECK_FALSE(written.settings.writeLine(0, line, sizeof(line)));
}

TEST_CASE("resetting the scaler settings leaves the AV module's picture alone")
{
    Stored stored;
    stored.av.contrast = 200;
    stored.av.svMode = 3;
    stored.volume = 23;
    stored.options.wantScanlines = 1;

    stored.settings.resetScalerSettings();

    CHECK(stored.options.wantScanlines == 0);
    CHECK(stored.av.contrast == 200);
    CHECK(stored.av.svMode == 3);
    CHECK(stored.volume == 23);
}

TEST_CASE("the chosen input is stored by the name the routes use")
{
    Stored written;
    VideoSourceSelection::select(VideoSourceSelection::Ypbpr);
    const std::string file = written.saved();
    CHECK(file.find("input = ypbpr\n") != std::string::npos);

    VideoSourceSelection::forgetSelection();
    Stored read;
    read.load(file);

    CHECK(VideoSourceSelection::selected() == VideoSourceSelection::Ypbpr);
}

TEST_CASE("an input nobody offers chooses nothing, so detection sweeps")
{
    Stored read;
    VideoSourceSelection::select(VideoSourceSelection::Vga);
    read.load("input = scart\n");

    CHECK(VideoSourceSelection::selected() == VideoSourceSelection::None);
}

TEST_CASE("defaults choose nothing, so a unit with no file sweeps")
{
    Stored stored;
    VideoSourceSelection::select(VideoSourceSelection::Vga);
    stored.settings.defaults();

    CHECK(VideoSourceSelection::selected() == VideoSourceSelection::None);
}
