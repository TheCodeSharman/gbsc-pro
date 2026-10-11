#include "Settings.h"

#include <string.h>

#include "../../options.h"
#include "../audio/LineVolume.h"
#include "../tv5725/ColourBalance.h"
#include "../tv5725/PictureOptions.h"
#include "../videosource/VideoSourceSelection.h"

namespace Prefs {

namespace {

const char Terminator[] = "end";

// The AV module's picture controls, where the middle of each range is neutral.
const uint8_t PictureNeutral = 128;
const uint8_t PictureLimit = 255;

// The broadcast standards the decoder can be told to expect.
const uint8_t StandardLimit = MT_MODE_SECAM;

const char *skipSpace(const char *at)
{
    while (*at == ' ' || *at == '\t')
        ++at;
    return at;
}

// A line, without its leading space, its trailing space or a line ending the
// filesystem left on it.
uint8_t trimmed(const char *&at, const char *end, char *into, uint8_t size)
{
    at = skipSpace(at);
    while (end > at && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                        end[-1] == '\n'))
        --end;

    uint8_t length = 0;
    for (; at + length < end && length + 1 < size; ++length)
        into[length] = at[length];
    into[length] = '\0';
    return length;
}

bool sameText(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

}  // namespace

Settings::Settings(userOptions &options, avOptions &av,
                   Tv5725::ColourBalance &colour,
                   Tv5725::PictureOptions &picture, uint8_t &volume,
                   uint8_t &legacyInput, uint8_t &brightnessSet)
    : options_(options), av_(av), colour_(colour), picture_(picture),
      volume_(volume), legacyInput_(legacyInput), brightnessSet_(brightnessSet)
{
}

const char *Settings::terminator() { return Terminator; }

void Settings::defaults()
{
    SettingVisitor visit = SettingVisitor::defaulting();
    each(visit);
}

void Settings::resetScalerSettings()
{
    SettingVisitor visit = SettingVisitor::defaulting();
    eachScalerSetting(visit);
}

Settings::Line Settings::readLine(const char *line)
{
    const char *at = skipSpace(line);
    if (*at == '\0' || *at == '#' || *at == '\r' || *at == '\n')
        return Skipped;

    const char *separator = at;
    while (*separator != '\0' && *separator != '=')
        ++separator;

    char key[28];
    const char *from = at;
    trimmed(from, separator, key, sizeof(key));

    if (*separator == '\0')
        return sameText(key, Terminator) ? End : Skipped;

    char value[12];
    from = separator + 1;
    trimmed(from, from + strlen(from), value, sizeof(value));

    SettingVisitor visit = SettingVisitor::reading(key, value);
    each(visit);
    return visit.found() ? Applied : Skipped;
}

bool Settings::writeLine(uint16_t index, char *out, uint8_t size)
{
    SettingVisitor visit = SettingVisitor::writing(index, out, size);
    each(visit);
    return visit.found();
}

void Settings::each(SettingVisitor &visit)
{
    eachScalerSetting(visit);
    eachBoardSetting(visit);
}

void Settings::eachScalerSetting(SettingVisitor &visit)
{
    // No name at all is what a missing key leaves, and the caller repairs it:
    // which modes exist is OutputMode's, and a text file cannot be trusted to
    // name one that does.
    visit.text("output", options_.outputResolution, OutputResolutionBytes, "");
    visit.character("slot", options_.presetSlot, 'A');

    visit.number("frame-time-lock", options_.enableFrameTimeLock, 1, 0);
    visit.number("frame-time-lock-method", options_.frameTimeLockMethod, 1, 0);
    visit.number("scanlines", options_.wantScanlines, 1, 0);
    visit.number("scanline-strength", options_.scanlineStrength, 0x60, 0x30);
    visit.number("deinterlace-mode", options_.deintMode, 2, 0);
    uint8_t lineFilter = picture_.lineFilter();
    uint8_t peaking = picture_.peaking();
    uint8_t sharpness = picture_.sharpness();
    uint8_t stepResponse = picture_.stepResponse();
    uint8_t autoGain = picture_.autoGain();
    uint8_t outputComponent = picture_.outputComponent();
    visit.number("auto-gain", autoGain, 1,
                 Tv5725::PictureOptions::AutoGainDefault);
    visit.number("component-output", outputComponent, 1,
                 Tv5725::PictureOptions::OutputComponentDefault);
    visit.number("line-filter", lineFilter, 1,
                 Tv5725::PictureOptions::LineFilterDefault);
    visit.number("peaking", peaking, 1, Tv5725::PictureOptions::PeakingDefault);
    visit.number("sharpness", sharpness, 1,
                 Tv5725::PictureOptions::SharpnessDefault);
    visit.number("step-response", stepResponse, 1,
                 Tv5725::PictureOptions::StepResponseDefault);
    picture_.setLineFilter(lineFilter);
    picture_.setPeaking(peaking);
    picture_.setSharpness(sharpness);
    picture_.setStepResponse(stepResponse);
    picture_.setAutoGain(autoGain);
    picture_.setOutputComponent(outputComponent);

    visit.number("six-tap", options_.wantTap6, 1, 1);
    visit.number("scale-rgbhv", options_.preferScalingRgbhv, 1, 0);
    visit.number("apply-aspect", options_.applyAspect, 1, 1);
    visit.number("pal-force-60", options_.PalForce60, 1, 0);
    visit.number("calibrate-adc", options_.enableCalibrationADC, 1, 1);
    visit.number("external-clock-off", options_.disableExternalClockGenerator,
                 1, 0);
}

void Settings::eachBoardSetting(SettingVisitor &visit)
{
    visit.number("volume", volume_, Audio::LineVolume::Maximum,
                 Audio::LineVolume::Default);

    // The chosen input, by the name the routes use, because nothing on the chip
    // reports it: half the path is the HC32F460's analog switches and those
    // cannot be read back at all. An empty name, which is what a missing key
    // leaves, chooses nothing -- and nothing chosen is what makes detection
    // sweep.
    char input[8];
    strncpy(input, VideoSourceSelection::name(VideoSourceSelection::selected()),
            sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    visit.text("input", input, sizeof(input), "");
    VideoSourceSelection::select(VideoSourceSelection::fromName(input));

    // Two lossy views of the input above, kept because detection still reads
    // the first and the OLED still writes the second. docs/known-issues.md
    visit.number("legacy-input", legacyInput_, 3, 0);
    visit.number("brightness-set", brightnessSet_, 2, 0);

    visit.number("sv-standard", av_.svMode, StandardLimit, 0);
    visit.number("av-standard", av_.avMode, StandardLimit, 0);
    visit.number("av-smooth", av_.smooth, 1, 0);
    visit.number("av-line-double", av_.lineDouble, 1, 0);
    visit.number("av-rgb-compatible", av_.rgbCompatible, 1, 0);
    visit.number("av-brightness", av_.bright, PictureLimit, PictureNeutral);
    visit.number("av-contrast", av_.contrast, PictureLimit, PictureNeutral);
    visit.number("av-saturation", av_.saturation, PictureLimit, PictureNeutral);

    uint8_t red = colour_.red();
    uint8_t green = colour_.green();
    uint8_t blue = colour_.blue();
    uint8_t lumaGain = colour_.lumaGain();
    visit.number("colour-red", red, Tv5725::ColourBalance::Limit,
                 Tv5725::ColourBalance::Neutral);
    visit.number("colour-green", green, Tv5725::ColourBalance::Limit,
                 Tv5725::ColourBalance::Neutral);
    visit.number("colour-blue", blue, Tv5725::ColourBalance::Limit,
                 Tv5725::ColourBalance::Neutral);
    visit.number("luma-gain", lumaGain, Tv5725::ColourBalance::Limit,
                 Tv5725::ColourBalance::Neutral);
    colour_.adopt(red, green, blue, lumaGain);
}

}  // namespace Prefs
