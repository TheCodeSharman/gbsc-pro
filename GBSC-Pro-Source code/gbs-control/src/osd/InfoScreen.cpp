#include "InfoScreen.h"

#include "OSD.h"
#include "Row.h"

namespace Osd {

const char InfoScreen::Bar;
const char InfoScreen::Title;
const char InfoScreen::Body;

namespace {

// The first row: what is going out. The resolution field is nine columns, which
// is what "1920x1080" takes and what leaves "Bypass" room beside the input.
const uint8_t OutputColumn = 5;
const uint8_t OutputDigits = 4;
const uint8_t InputColumn = 15;
const uint8_t OutputRateColumn = 21;
const uint8_t RateDigits = 2;
const uint8_t OutputUnitColumn = 23;

// The second row: what was measured. The count is four columns because a frame
// is never five, and the scan letter sits against it rather than spaced off it.
const uint8_t LinesColumn = 3;
const uint8_t LinesDigits = 4;
const uint8_t ScanColumn = 7;
const uint8_t FieldRateColumn = 9;
const uint8_t FieldUnitColumn = 11;
const uint8_t LineRateColumn = 14;

}  // namespace

void InfoScreen::draw(const Report &report)
{
    Row out(Bar, Bar);
    out.text(0, "Out:", Title);
    if (report.bypass) {
        out.text(OutputColumn, "Bypass", Body);
    } else {
        out.number(OutputColumn, report.outputPx, OutputDigits, Body);
        out.text((uint8_t)(OutputColumn + OutputDigits), "x", Body);
        out.number((uint8_t)(OutputColumn + OutputDigits + 1),
                   report.outputLines, OutputDigits, Body);
    }
    out.text(InputColumn, report.input, Body);

    // Nothing of ours times the output in pass-through, the frame rate being
    // read off the VDS that the video does not go through there. Zero is the
    // absence of a measurement, so it is shown as one.
    if (report.outputRateHz != 0) {
        out.number(OutputRateColumn, report.outputRateHz, RateDigits, Body);
        out.text(OutputUnitColumn, "Hz", Body);
    }
    OSD::send(0, out);

    Row in(Bar, Bar);
    in.text(0, "In:", Title);
    if (!report.present) {
        in.text(LinesColumn + 3, "no signal", Body);
    } else {
        in.number(LinesColumn, report.lines, LinesDigits, Body);
        in.text(ScanColumn, report.interlaced ? "i" : "p", Body);
        in.number(FieldRateColumn, report.fieldRateHz, RateDigits, Body);
        in.text(FieldUnitColumn, "Hz", Body);

        // The line rate to a tenth of a kilohertz, which is what tells two
        // modes of the same height apart. Rounded rather than truncated: 31469
        // is 31.5, and a tenth that always reads low is a tenth nobody trusts.
        const uint16_t tenths = (uint16_t)((report.lineRateHz + 50) / 100);
        in.number(LineRateColumn, (uint16_t)(tenths / 10), 2, Body);
        in.text((uint8_t)(LineRateColumn + 2), ".", Body);
        in.number((uint8_t)(LineRateColumn + 3), (uint16_t)(tenths % 10), 1, Body);
        in.text((uint8_t)(LineRateColumn + 4), "kHz", Body);
    }
    OSD::send(1, in);
}

}  // namespace Osd
