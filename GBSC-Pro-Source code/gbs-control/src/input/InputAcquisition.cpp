#include "InputAcquisition.h"

InputAcquisition::InputAcquisition(Tv5725::VideoPath &videoPath)
    : videoPath_(videoPath) {}

bool InputAcquisition::poll(uint32_t nowMs)
{
    return videoPath_.poll(nowMs);
}
