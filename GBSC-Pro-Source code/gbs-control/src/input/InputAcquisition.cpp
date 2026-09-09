#include "InputAcquisition.h"

InputAcquisition::InputAcquisition(Tv5725::VideoPath &videoPath)
    : videoPath_(videoPath), detectedMs_(0), detectedEver_(false) {}

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
    return videoPath_.poll(detectionDue(nowMs));
}
