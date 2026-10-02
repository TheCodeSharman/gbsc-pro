#include "VolumeOverlay.h"

#include "OSD.h"
#include "Row.h"

namespace Osd {

const char VolumeOverlay::Bar;
const char VolumeOverlay::Title;
const char VolumeOverlay::Body;

namespace {

// Where the chain put them, so the overlay does not move under anyone.
const uint8_t LabelColumn = 1;
const uint8_t LevelColumn = 20;
const uint8_t LevelDigits = 2;

}  // namespace

void VolumeOverlay::draw(uint8_t level)
{
    Row row(Bar, Bar);
    row.text(LabelColumn, "Line input volume", Title);
    row.number(LevelColumn, level, LevelDigits, Body);
    OSD::send(0, row);
}

}  // namespace Osd
