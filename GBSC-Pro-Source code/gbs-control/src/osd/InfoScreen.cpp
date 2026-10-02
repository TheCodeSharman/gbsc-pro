#include "InfoScreen.h"

#include <stddef.h>

#include "OSD.h"
#include "Row.h"

namespace Osd {

const char InfoScreen::Bar;
const char InfoScreen::Title;
const char InfoScreen::Body;

namespace {

// Where the chain put them, so the screen does not move under anyone.
const uint8_t OutputColumn = 6;
const uint8_t OutputDigits = 4;
const uint8_t InputColumn = 18;
const uint8_t RateColumn = 24;
const uint8_t RateDigits = 2;
const uint8_t UnitColumn = 26;

// The second row. The chain's own columns again: the kind starts where its
// string did, and the scan token lands on the four columns that held `576i`.
const uint8_t RgbColumn = 9;
const uint8_t ComponentColumn = 11;
const uint8_t VideoColumn = 12;
const uint8_t LinesColumn = 20;
const uint8_t LinesDigits = 4;
const uint8_t ScanColumn = 24;

// The third row, which the chain only ever had a commented-out version string
// on. The value alone, right-justified against the last column: three columns
// of kilohertz, a point, a tenth and the unit.
const uint8_t LineRateColumn = 20;
const uint8_t LineRateDigits = 3;

struct Named {
    uint8_t column;
    const char *text;
};

Named kindOf(const InfoScreen::Report &report)
{
    switch (report.kind) {
        case InfoScreen::Rgb: {
            const Named named = { RgbColumn,
                                  report.separateSync ? "RGB HV" : "RGB" };
            return named;
        }
        case InfoScreen::Component: {
            const Named named = { ComponentColumn, "YPBPR" };
            return named;
        }
        case InfoScreen::SVideo: {
            const Named named = { VideoColumn, "SV" };
            return named;
        }
        case InfoScreen::Composite: {
            const Named named = { VideoColumn, "AV" };
            return named;
        }
        default: break;
    }
    const Named named = { RgbColumn, "No Input" };
    return named;
}

}  // namespace

void InfoScreen::draw(const Report &report)
{
    Row out(Bar, Bar);
    out.text(0, "Info:", Title);
    if (report.bypass) {
        out.text(OutputColumn, "Bypass", Body);
    } else {
        out.number(OutputColumn, report.outputPx, OutputDigits, Body);
        out.text((uint8_t)(OutputColumn + OutputDigits), "x", Body);
        out.number((uint8_t)(OutputColumn + OutputDigits + 1),
                   report.outputLines, OutputDigits, Body);
    }
    out.text(InputColumn, report.input, Body);
    if (report.present) {
        out.number(RateColumn, report.rateHz, RateDigits, Body);
        out.text(UnitColumn, "Hz", Body);
    }
    OSD::send(0, out);

    Row in(Bar, Bar);
    in.text(0, "Current:", Title);
    const Named kind = kindOf(report);
    in.text(kind.column, kind.text, Body);
    if (report.present) {
        in.number(LinesColumn, report.lines, LinesDigits, Body);
        in.text(ScanColumn, report.interlaced ? "i" : "p", Body);
    }
    OSD::send(1, in);

    Row rate(Bar, Bar);
    if (report.present) {
        // To a tenth of a kilohertz, rounded rather than truncated: 31469 is
        // 31.5, and a tenth that always reads low is a tenth nobody trusts.
        const uint16_t tenths = (uint16_t)((report.lineRateHz + 50) / 100);
        rate.number(LineRateColumn, (uint16_t)(tenths / 10), LineRateDigits,
                    Body);
        rate.text((uint8_t)(LineRateColumn + LineRateDigits), ".", Body);
        rate.number((uint8_t)(LineRateColumn + LineRateDigits + 1),
                    (uint16_t)(tenths % 10), 1, Body);
        rate.text((uint8_t)(LineRateColumn + LineRateDigits + 2), "kHz", Body);
    }
    OSD::send(2, rate);
}

}  // namespace Osd
