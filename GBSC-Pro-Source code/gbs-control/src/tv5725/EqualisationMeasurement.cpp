#include "EqualisationMeasurement.h"

#include "VideoSignal.h"

namespace Tv5725 {

bool EqualisationMeasurement::equalised_ = false;
bool EqualisationMeasurement::set_ = false;

const uint8_t EqualisationMeasurement::EqualisationLines;
const uint8_t EqualisationMeasurement::Samples;

namespace {

// Insertion sort over Samples readings, which is the whole of it at this size.
uint16_t medianOf(uint16_t (&taken)[EqualisationMeasurement::Samples])
{
    for (uint8_t i = 1; i < EqualisationMeasurement::Samples; i++) {
        const uint16_t value = taken[i];
        int8_t j = (int8_t)(i - 1);
        for (; j >= 0 && taken[j] > value; j--)
            taken[j + 1] = taken[j];
        taken[j + 1] = value;
    }
    return taken[EqualisationMeasurement::Samples / 2];
}

uint16_t sampleAt(EqualisationMeasurement::CountAtThreshold countAt,
                  bool rejectsEqualisation)
{
    uint16_t taken[EqualisationMeasurement::Samples];
    for (uint8_t i = 0; i < EqualisationMeasurement::Samples; i++)
        taken[i] = countAt(rejectsEqualisation);
    return medianOf(taken);
}

}  // namespace

bool EqualisationMeasurement::carriesEqualisation() { return equalised_; }

bool EqualisationMeasurement::isSet() { return set_; }

bool EqualisationMeasurement::assumeEqualisation() { return !set_ || equalised_; }

void EqualisationMeasurement::set(bool equalised) { equalised_ = equalised; }

void EqualisationMeasurement::forget()
{
    equalised_ = false;
    set_ = false;
}

bool EqualisationMeasurement::equalisation(CountAtThreshold countAt)
{
    if (!set_)
        probe(countAt);
    return equalised_;
}

// The lower threshold admits whatever the vertical interval carries at
// half-line spacing, so a serrated source counts those as lines and reads
// HIGH. An unserrated one reads the same through both.
bool EqualisationMeasurement::probe(CountAtThreshold countAt)
{
    const uint16_t admitting = sampleAt(countAt, false);
    const uint16_t rejecting = sampleAt(countAt, true);

    if (!VideoSignal::countIsSource(admitting)
        || !VideoSignal::countIsSource(rejecting))
        return equalised_;

    equalised_ = admitting >= (uint16_t)(rejecting + EqualisationLines);
    set_ = true;
    return equalised_;
}

}  // namespace Tv5725
