#include "MuteOverlay.h"

#include "OSD.h"
#include "Row.h"
#include "VolumeOverlay.h"

namespace Osd {

void MuteOverlay::draw(bool muted)
{
    Row row(VolumeOverlay::Bar, VolumeOverlay::Bar);
    row.text(1, "MUTE", VolumeOverlay::Title);
    row.text(6, muted ? "ON" : "OFF", VolumeOverlay::Body);
    OSD::send(0, row);
}

}  // namespace Osd
