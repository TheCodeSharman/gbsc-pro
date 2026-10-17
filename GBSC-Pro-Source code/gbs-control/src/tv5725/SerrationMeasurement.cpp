#include "SerrationMeasurement.h"

#include "VideoSignal.h"

namespace Tv5725 {

bool SerrationMeasurement::serrated_ = false;
bool SerrationMeasurement::set_ = false;

const uint8_t SerrationMeasurement::SerrationLines;
const uint8_t SerrationMeasurement::Samples;

namespace {

// Insertion sort over Samples readings, which is the whole of it at this size.
uint16_t medianOf(uint16_t (&taken)[SerrationMeasurement::Samples])
{
    for (uint8_t i = 1; i < SerrationMeasurement::Samples; i++) {
        const uint16_t value = taken[i];
        int8_t j = (int8_t)(i - 1);
        for (; j >= 0 && taken[j] > value; j--)
            taken[j + 1] = taken[j];
        taken[j + 1] = value;
    }
    return taken[SerrationMeasurement::Samples / 2];
}

uint16_t sampleAt(SerrationMeasurement::CountAtThreshold countAt,
                  bool rejectsSerrations)
{
    uint16_t taken[SerrationMeasurement::Samples];
    for (uint8_t i = 0; i < SerrationMeasurement::Samples; i++)
        taken[i] = countAt(rejectsSerrations);
    return medianOf(taken);
}

}  // namespace

bool SerrationMeasurement::isSerrated() { return serrated_; }

bool SerrationMeasurement::isSet() { return set_; }

bool SerrationMeasurement::assumeSerrated() { return !set_ || serrated_; }

void SerrationMeasurement::set(bool serrated) { serrated_ = serrated; }

void SerrationMeasurement::forget()
{
    serrated_ = false;
    set_ = false;
}

bool SerrationMeasurement::serration(CountAtThreshold countAt)
{
    if (!set_)
        probe(countAt);
    return serrated_;
}

// The lower threshold admits whatever the vertical interval carries at
// half-line spacing, so a serrated source counts those as lines and reads
// HIGH. An unserrated one reads the same through both.
bool SerrationMeasurement::probe(CountAtThreshold countAt)
{
    const uint16_t admitting = sampleAt(countAt, false);
    const uint16_t rejecting = sampleAt(countAt, true);

    if (!VideoSignal::countIsSource(admitting)
        || !VideoSignal::countIsSource(rejecting))
        return serrated_;

    serrated_ = admitting >= (uint16_t)(rejecting + SerrationLines);
    set_ = true;
    return serrated_;
}

}  // namespace Tv5725
