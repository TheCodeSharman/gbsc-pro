#include "SyncOnGreen.h"

namespace Tv5725 {

uint8_t SyncOnGreen::level_ = 0;

void SyncOnGreen::choose(uint8_t level)
{
    if (level > LevelMax)
        return;

    level_ = level;
}

void SyncOnGreen::apply(uint8_t level)
{
    choose(level);
    apply();
}

void SyncOnGreen::apply() { ADC_SOGCTRL::write(level_); }

uint8_t SyncOnGreen::level() { return level_; }

}  // namespace Tv5725
