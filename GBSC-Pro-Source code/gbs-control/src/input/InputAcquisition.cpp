#include "InputAcquisition.h"

InputAcquisition::InputAcquisition(Tv5725::SourceMeasurement &sampling,
                                   Tv5725::VideoPath &videoPath)
    : sampling_(sampling), videoPath_(videoPath), mayRun_(0), detectedMs_(0),
      detectedEver_(false) {}

void InputAcquisition::useRunGate(bool (*mayRun)()) { mayRun_ = mayRun; }

float InputAcquisition::sourceFieldRateHz() const { return sampling_.fieldRateHz(); }

uint32_t InputAcquisition::sourceLineRateHz() const { return sampling_.heldLineRateHz(); }

bool InputAcquisition::sourceLowLineRate() const { return sampling_.lowLineRate(); }

bool InputAcquisition::detectionDue(uint32_t nowMs)
{
    if (detectedEver_ && nowMs - detectedMs_ < DetectionIntervalMs)
        return false;
    detectedMs_ = nowMs;
    detectedEver_ = true;
    return true;
}

bool InputAcquisition::poll(uint32_t nowMs)
{
    if (mayRun_ != 0 && !mayRun_())
        return false;
    return videoPath_.poll(detectionDue(nowMs));
}
