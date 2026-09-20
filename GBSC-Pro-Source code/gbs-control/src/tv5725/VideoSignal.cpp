#include "VideoSignal.h"

namespace Tv5725 {

const uint16_t VideoSignal::SourceVerticalTotalMin;
const uint16_t VideoSignal::SourceVerticalTotalMax;

bool VideoSignal::countIsSource(uint16_t lines)
{
    return lines >= SourceVerticalTotalMin && lines <= SourceVerticalTotalMax;
}

bool VideoSignal::fieldRateIsSource(float fieldRateHz)
{
    return fieldRateHz >= FieldRateMinHz && fieldRateHz <= FieldRateMaxHz;
}

bool VideoSignal::isVideo(uint16_t sourceLines, float fieldRateHz)
{
    return countIsSource(sourceLines) && fieldRateIsSource(fieldRateHz);
}

uint32_t VideoSignal::lineRateFor(uint16_t sourceLines, float fieldRateHz)
{
    return (uint32_t)(fieldRateHz * (float)(sourceLines + 1));
}

bool VideoSignal::ratesAgree(uint32_t a, uint32_t b, uint16_t perMille)
{
    const uint32_t larger = a > b ? a : b;
    const uint32_t smaller = a > b ? b : a;
    return (larger - smaller) * 1000u <= (uint32_t)perMille * smaller;
}

}  // namespace Tv5725
