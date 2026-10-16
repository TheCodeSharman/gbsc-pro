#include "AutoGain.h"

#include <Arduino.h>   // delay(), a hardware settling time
#include <stdio.h>

#include "Adc.h"
#include "TestBus.h"
#include "Tv5725Log.h"

namespace Tv5725 {

namespace {

// The highest level the channel reached, and how many of the reads were in the
// band -- both off one run of samples, because two runs describe two different
// moments of the picture.
//
// The wait is the selection settling. Read straight after it, the bus answers
// with every bit set, which masks to the top of the range and counts as a
// clipped sample at any gain.
AutoGain::Reading readingOf(TestBus::Channel channel)
{
    TestBus::selectLevel(channel);
    delay(1);

    AutoGain::Reading reading = {0, 0};
    for (uint8_t i = 0; i < AutoGain::SamplesPerChannel; ++i) {
        const uint16_t level = TestBus::readLevel();
        if (level > reading.peak)
            reading.peak = level;
        if (level >= AutoGain::BackOffLevel)
            ++reading.over;
    }
    return reading;
}

}  // namespace

const uint16_t AutoGain::Saturated;
const uint16_t AutoGain::Headroom;
const uint16_t AutoGain::BackOffLevel;
const uint16_t AutoGain::EvidenceLevel;
const uint8_t AutoGain::Step;
const uint8_t AutoGain::SamplesPerChannel;
const uint8_t AutoGain::OverToStep;
const uint8_t AutoGain::EvidenceRun;
const uint16_t AutoGain::SettleMs;
const uint16_t AutoGain::QuietMs;
const uint16_t AutoGain::SettledMs;

uint32_t AutoGain::lastLookMs_ = 0;
uint8_t AutoGain::evidenceRun_ = 0;
bool AutoGain::looked_ = false;
bool AutoGain::overSeen_ = false;
bool AutoGain::settled_ = false;

void AutoGain::forget()
{
    looked_ = false;
    overSeen_ = false;
    settled_ = false;
    evidenceRun_ = 0;
}

bool AutoGain::saturated(const Levels &measured)
{
    const uint16_t over = (uint16_t)measured.red.over + measured.green.over
                          + measured.blue.over;
    return over >= OverToStep;
}

uint8_t AutoGain::gainFor(uint8_t inForce, const Levels &measured)
{
    if (!saturated(measured))
        return inForce;
    if (inForce > (uint8_t)(0xFF - Step))
        return 0xFF;
    return (uint8_t)(inForce + Step);
}

AutoGain::Levels AutoGain::peaks()
{
    const TestBus::Hold held;

    Levels measured;
    measured.red = readingOf(TestBus::Red);
    measured.green = readingOf(TestBus::Green);
    measured.blue = readingOf(TestBus::Blue);
    return measured;
}

void AutoGain::judgeSettling(bool over, const Levels &measured)
{
    if (over) {
        evidenceRun_ = 0;
        settled_ = false;
        return;
    }

    const bool bright = measured.red.peak >= EvidenceLevel
                        || measured.green.peak >= EvidenceLevel
                        || measured.blue.peak >= EvidenceLevel;
    if (!bright)
        return;

    if (evidenceRun_ < EvidenceRun)
        ++evidenceRun_;
    if (evidenceRun_ < EvidenceRun || settled_)
        return;

    settled_ = true;

    char line[48];
    snprintf(line, sizeof(line), "auto gain: settled at %u", Adc::gain());
    tv5725Log(line);
}

bool AutoGain::run(uint32_t nowMs)
{
    const uint16_t due = overSeen_ ? SettleMs : settled_ ? SettledMs : QuietMs;
    if (looked_ && nowMs - lastLookMs_ < due)
        return false;

    lastLookMs_ = nowMs;
    looked_ = true;

    const Levels measured = peaks();
    const bool over = saturated(measured);
    const bool confirmed = over && overSeen_;
    overSeen_ = over;

    judgeSettling(over, measured);

    if (!confirmed)
        return false;

    const uint8_t inForce = Adc::gain();
    const uint8_t wanted = gainFor(inForce, measured);

    if (wanted == inForce)
        return false;

    Adc::holdGain(wanted, wanted, wanted);

    char line[80];
    snprintf(line, sizeof(line),
             "auto gain: peaks %u/%u/%u over %u/%u/%u, gain %u -> %u",
             measured.red.peak, measured.green.peak, measured.blue.peak,
             measured.red.over, measured.green.over, measured.blue.over,
             inForce, wanted);
    tv5725Log(line);

    return true;
}

}  // namespace Tv5725
