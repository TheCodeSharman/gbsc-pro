#include "VideoSourceSelection.h"

namespace {

struct Row {
    VideoSourceSelection::Id id;
    const char *name;
    VideoSourceSelection::Settings settings;
};

// frame, legacySource, brightnessSet, writesAdc, adcInputSel, adcSogEn,
// extSyncSel, clearsLowPower
const Row Rows[] = {
    {VideoSourceSelection::Rgbs,      "rgbs",  {0x40, 1, 0, true,  1, 1, 1, false}},
    {VideoSourceSelection::RgsB,      "rgsb",  {0x50, 1, 0, true,  1, 1, 1, false}},
    {VideoSourceSelection::Vga,       "vga",   {0x61, 2, 0, true,  1, 1, 0, false}},
    {VideoSourceSelection::Ypbpr,     "ypbpr", {0x70, 3, 1, true,  0, 0, 1, false}},
    {VideoSourceSelection::SVideo,    "sv",    {0x10, 3, 2, true,  0, 0, 1, true}},
    {VideoSourceSelection::Composite, "av",    {0x20, 3, 2, true,  0, 0, 1, true}},
};

const uint8_t RowCount = sizeof(Rows) / sizeof(Rows[0]);

bool sameName(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

}  // namespace

bool VideoSourceSelection::chosen(uint8_t id)
{
    return id >= Rgbs && id <= Composite;
}

VideoSourceSelection::Settings VideoSourceSelection::settingsFor(Id id)
{
    for (uint8_t i = 0; i < RowCount; ++i)
        if (Rows[i].id == id)
            return Rows[i].settings;

    // Not reachable through chosen(), and a caller that skipped it gets an
    // input that selects nothing rather than one that selects the wrong thing.
    Settings none = {0, 0, 0, false, 0, 0, 1, false};
    return none;
}

bool VideoSourceSelection::syncTypeMustBeMeasured(Id id)
{
    return chosen(id) && settingsFor(id).extSyncSel == 0;
}

uint8_t VideoSourceSelection::port(Id id)
{
    return settingsFor(id).adcInputSel;
}

bool VideoSourceSelection::sharesPort(Id a, Id b)
{
    return chosen(a) && chosen(b) && port(a) == port(b);
}

VideoSourceSelection::Id VideoSourceSelection::fromStored(uint8_t info)
{
    return chosen(info) ? (Id)info : None;
}

VideoSourceSelection::Id VideoSourceSelection::fromName(const char *name)
{
    if (name == 0)
        return None;

    for (uint8_t i = 0; i < RowCount; ++i)
        if (sameName(name, Rows[i].name))
            return Rows[i].id;
    return None;
}

const char *VideoSourceSelection::name(Id id)
{
    for (uint8_t i = 0; i < RowCount; ++i)
        if (Rows[i].id == id)
            return Rows[i].name;
    return "";
}
