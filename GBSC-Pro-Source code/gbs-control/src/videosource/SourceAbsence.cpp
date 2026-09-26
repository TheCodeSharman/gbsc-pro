#include "SourceAbsence.h"

const uint8_t SourceAbsence::PassesBeforeLowPower;

SourceAbsence::SourceAbsence() : passes_(0) {}

void SourceAbsence::found() { passes_ = 0; }

void SourceAbsence::missed()
{
    if (passes_ < PassesBeforeLowPower)
        ++passes_;
}

void SourceAbsence::undecided()
{
    missed();
}

void SourceAbsence::selectionChanged()
{
    passes_ = PassesBeforeLowPower - 1;
}

void SourceAbsence::poweredDown()
{
    passes_ = 0;
}

bool SourceAbsence::shouldPowerDown() const
{
    return passes_ >= PassesBeforeLowPower;
}

uint8_t SourceAbsence::passes() const { return passes_; }
