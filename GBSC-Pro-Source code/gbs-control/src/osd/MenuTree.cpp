#include "MenuTree.h"

#include "../../options.h"
#include "../tv5725/Aspect.h"
#include "../tv5725/Controls.h"
#include "../tv5725/HdBypass.h"
#include "../tv5725/OutputMode.h"
#include "../tv5725/Nudge.h"
#include "../tv5725/VideoPath.h"
#include "MenuContext.h"

namespace Osd {

namespace {

const char *onOff(uint8_t value) { return value ? "ON" : "OFF"; }

const char *autoGainText(const MenuContext &context)
{
    return onOff(context.options().enableAutoGain);
}

const char *scanlinesText(const MenuContext &context)
{
    return onOff(context.options().wantScanlines);
}

const char *lineFilterText(const MenuContext &context)
{
    return onOff(context.options().wantVdsLineFilter);
}

const char *peakingText(const MenuContext &context)
{
    return onOff(context.options().wantPeaking);
}

const char *sharpnessText(const MenuContext &context)
{
    return onOff(context.options().wantSharpness);
}

const char *stepResponseText(const MenuContext &context)
{
    return onOff(context.options().wantStepResponse);
}

// A balance reads as the number the chain's overlay drew, three digits wide.
// One buffer per row, because a page resolves three of them.
const char *decimal(char *into, uint8_t value)
{
    into[0] = (char)('0' + value / 100);
    into[1] = (char)('0' + (value % 100) / 10);
    into[2] = (char)('0' + value % 10);
    into[3] = '\0';
    return into;
}

Tv5725::ColourBalance &balanceOf(const MenuContext &context)
{
    return context.controls().engine().colour();
}

const char *redText(const MenuContext &context)
{
    static char text[4];
    return decimal(text, balanceOf(context).red());
}

const char *greenText(const MenuContext &context)
{
    static char text[4];
    return decimal(text, balanceOf(context).green());
}

const char *blueText(const MenuContext &context)
{
    static char text[4];
    return decimal(text, balanceOf(context).blue());
}

const char *lumaGainText(const MenuContext &context)
{
    static char text[4];
    return decimal(text, balanceOf(context).lumaGain());
}

const char *doubleLineText(const MenuContext &context)
{
    return context.av().lineDouble ? "2X" : "1X";
}

const char *smoothText(const MenuContext &context)
{
    return onOff(context.av().smooth);
}

const char *compatibilityText(const MenuContext &context)
{
    return onOff(context.av().rgbCompatible);
}

const char *brightText(const MenuContext &context)
{
    static char text[4];
    return decimal(text, context.av().bright);
}

const char *contrastText(const MenuContext &context)
{
    static char text[4];
    return decimal(text, context.av().contrast);
}

const char *saturationText(const MenuContext &context)
{
    static char text[4];
    return decimal(text, context.av().saturation);
}

// The broadcast standards the decoder can be told to expect, in the order the
// frame's mode table carries them.
const char *const Formats[] = {
    "Auto",   "PAL",       "NTSC-M", "PAL-60",       "NTSC443",
    "NTSC-J", "PAL-N w/p", "PAL-M",  "PAL-M w/o p",  "PAL Cmb-N",
    "PAL Cmb-N w/p", "SECAM",
};

// One row for both decoder inputs, reporting whichever is selected. Composite
// and S-Video are the only two that reach the ADV7280, and a unit on neither
// shows what S-Video would get.
uint8_t formatOf(const MenuContext &context)
{
    return VideoSourceSelection::selected() == VideoSourceSelection::Composite
               ? context.av().avMode
               : context.av().svMode;
}

const char *formatText(const MenuContext &context)
{
    const uint8_t mode = formatOf(context);
    return mode < sizeof(Formats) / sizeof(Formats[0]) ? Formats[mode] : "Auto";
}

// Declaring no shape and declaring the panel's are the same picture, so a
// filled one reads as the panel's rather than as a shape of its own.
const char *aspectText(const MenuContext &context)
{
    switch (context.controls().engine().aspect().tenThousandths()) {
    case Tv5725::Aspect::FourThree:
        return "4:3";
    case Tv5725::Aspect::FiveFour:
        return "5:4";
    default:
        return "16:9";
    }
}

const char *upscalingText(const MenuContext &context)
{
    return onOff(context.options().preferScalingRgbhv);
}

const char *deinterlaceText(const MenuContext &context)
{
    return context.options().deintMode ? "Bob" : "Adaptive";
}

const char *frameTimeLockText(const MenuContext &context)
{
    return onOff(context.options().enableFrameTimeLock);
}

const char *lockMethodText(const MenuContext &context)
{
    return context.options().frameTimeLockMethod ? "Vtotal only" : "Vtotal+VSST";
}

const char *adcCalibrationText(const MenuContext &context)
{
    return onOff(context.options().enableCalibrationADC);
}

const char *clockGeneratorText(const MenuContext &context)
{
    return onOff(!context.options().disableExternalClockGenerator);
}

const MenuItem Input[] = {
    MenuItem::inputAction("RGBs", VideoSourceSelection::Rgbs),
    MenuItem::inputAction("RGsB", VideoSourceSelection::RgsB),
    MenuItem::inputAction("VGA", VideoSourceSelection::Vga),
    MenuItem::inputAction("YPBPR", VideoSourceSelection::Ypbpr),
    MenuItem::inputAction("SV", VideoSourceSelection::SVideo),
    MenuItem::inputAction("AV", VideoSourceSelection::Composite),
};

// Whether the source can be handed to the encoder at all, and whether it is
// being. Bypass passes the source's own timing through, so a rate the display
// refuses puts torn content on the panel -- the entry refuses it for that
// reason, and the row says so before the user presses Ok.
// docs/rgbhv-bypass-trap.md
const char *passThroughState(const MenuContext &context)
{
    const Tv5725::VideoPath &engine = context.controls().engine();
    if (!Tv5725::HdBypass::suitsLineRate(engine.sourceLineRateHz()))
        return "N/A";
    const Tv5725::OutputMode *const mode = engine.outputMode();
    return mode != NULL && mode->isBypass() ? "ON" : "OFF";
}

// Pass Through sits with the resolutions because that is where a user looks for
// it: it is the other destination the picture can have, even though
// an output mode cannot express it and the preference behind it lives under
// System Settings. Choosing any resolution above LEAVES it.
const MenuItem Resolution[] = {
    MenuItem::action("1920x1080", 's'),
    MenuItem::action("1280x1024", 'p'),
    MenuItem::action("1280x960", 'f'),
    MenuItem::action("1280x720", 'g'),
    MenuItem::action("768x576", 'j'),
    MenuItem::action("720x480", 'h'),
    MenuItem::serialChoice("Pass Through", 'K', passThroughState),
};

// Pan, zoom and the shape are all transforms the SCALER performs, and
// pass-through hands the source to the encoder without it. The engine refuses
// all three there and no register distinguishes the refusal from the press
// never arriving. docs/rgbhv-bypass-trap.md
bool scalerInPath(const MenuContext &context)
{
    const Tv5725::OutputMode *const mode =
        context.controls().engine().outputMode();
    return mode == NULL || !mode->isBypass();
}

// A pad asks for a control and the way it goes, which the remote's hold ramp
// multiplies -- the /sc? geometry letters are stated in output pixels, where one
// tap asks for one capture granule. The key follows the edge that moves: the
// picture is pinned at the top of the active region, so Down grows it.
const MenuPad MovePad(MenuCommand::nudge(Tv5725::Nudge::VerticalPan, +1),
                      MenuCommand::nudge(Tv5725::Nudge::VerticalPan, -1),
                      MenuCommand::nudge(Tv5725::Nudge::HorizontalPan, +1),
                      MenuCommand::nudge(Tv5725::Nudge::HorizontalPan, -1));

const MenuPad ScalePad(MenuCommand::nudge(Tv5725::Nudge::VerticalZoom, -1),
                       MenuCommand::nudge(Tv5725::Nudge::VerticalZoom, +1),
                       MenuCommand::nudge(Tv5725::Nudge::HorizontalZoom, -1),
                       MenuCommand::nudge(Tv5725::Nudge::HorizontalZoom, +1));

// Reset is live in both paths: it puts the stored framing and shape back, and
// both outlive the path the picture is currently taking.
const MenuItem Screen[] = {
    MenuItem::pad("Move", MovePad).onlyWhen(scalerInPath),
    MenuItem::pad("Scale", ScalePad).onlyWhen(scalerInPath),
    MenuItem::choice("Aspect", 'G', aspectText).onlyWhen(scalerInPath),
    MenuItem::serialAction("Reset", 'B'),
};

const MenuItem Picture[] = {
    MenuItem::adjust("ADC gain", MenuCommand::serial('T'), MenuCommand::user('n'),
                     MenuCommand::user('o'), autoGainText),
    MenuItem::adjust("Scanlines", MenuCommand::user('7'), MenuCommand::user('K'),
                     MenuCommand::user('K'), scanlinesText),
    MenuItem::choice("Line filter", 'm', lineFilterText),
    MenuItem::choice("Sharpness", 'W', sharpnessText),
    MenuItem::serialChoice("Peaking", 'f', peakingText),
    MenuItem::serialChoice("Step response", 'V', stepResponseText),
    MenuItem::adjust("R", MenuCommand::user('Y'),
                     MenuCommand::tune(Tune::Red, +1),
                     MenuCommand::tune(Tune::Red, -1), redText),
    MenuItem::adjust("G", MenuCommand::user('Y'),
                     MenuCommand::tune(Tune::Green, +1),
                     MenuCommand::tune(Tune::Green, -1), greenText),
    MenuItem::adjust("B", MenuCommand::user('Y'),
                     MenuCommand::tune(Tune::Blue, +1),
                     MenuCommand::tune(Tune::Blue, -1), blueText),
    MenuItem::adjust("Y gain", MenuCommand::user('Y'),
                     MenuCommand::tune(Tune::LumaGain, +1),
                     MenuCommand::tune(Tune::LumaGain, -1), lumaGainText),
    MenuItem::adjust("Colour", MenuCommand(), MenuCommand::user('V'),
                     MenuCommand::user('R'), NULL),
    MenuItem::action("Default colour", 'U'),
};

// The AV module's own picture: the decoder's standard, the ADV7391's line
// doubling and smoothing, its three picture controls, and the compatibility
// preference the RGB inputs share. None of it can be read back -- the HC32's
// UART reply pin goes to the update button -- so each row reports what is held.
const MenuItem SvAv[] = {
    MenuItem::adjust("Format", MenuCommand(),
                     MenuCommand::tune(Tune::Format, +1),
                     MenuCommand::tune(Tune::Format, -1), formatText),
    MenuItem::choice("DoubleLine", 'b', doubleLineText),
    MenuItem::choice("Smooth", 'c', smoothText),
    MenuItem::adjust("Bright", MenuCommand::user('Y'),
                     MenuCommand::tune(Tune::Brightness, +1),
                     MenuCommand::tune(Tune::Brightness, -1), brightText),
    MenuItem::adjust("Contrast", MenuCommand::user('Y'),
                     MenuCommand::tune(Tune::Contrast, +1),
                     MenuCommand::tune(Tune::Contrast, -1), contrastText),
    MenuItem::adjust("Saturation", MenuCommand::user('Y'),
                     MenuCommand::tune(Tune::Saturation, +1),
                     MenuCommand::tune(Tune::Saturation, -1), saturationText),
    MenuItem::action("Default", 'k'),
    MenuItem::choice("Compatibility", 'd', compatibilityText),
};

const MenuItem System[] = {
    MenuItem::submenu("Sv-Av InPutSet", SvAv, sizeof(SvAv) / sizeof(SvAv[0])),
    MenuItem::choice("Use upscaling", 'x', upscalingText),
    MenuItem::adjust("Deinterlace", MenuCommand(), MenuCommand::user('q'),
                     MenuCommand::user('r'), deinterlaceText),
    MenuItem::choice("Frame Time Lock", '5', frameTimeLockText),
    MenuItem::choice("Lock Method", 'i', lockMethodText),
    MenuItem::choice("ADC calibration", 'w', adcCalibrationText),
    MenuItem::choice("Clock generator", 'X', clockGeneratorText),
    MenuItem::action("Restart", 'a'),
};

const MenuItem Root[] = {
    MenuItem::submenu("Input", Input, sizeof(Input) / sizeof(Input[0])),
    MenuItem::submenu("Output Resolution", Resolution,
                      sizeof(Resolution) / sizeof(Resolution[0])),
    MenuItem::submenu("Screen Settings", Screen,
                      sizeof(Screen) / sizeof(Screen[0])),
    MenuItem::submenu("System Settings", System,
                      sizeof(System) / sizeof(System[0])),
    MenuItem::submenu("Picture Settings", Picture,
                      sizeof(Picture) / sizeof(Picture[0])),
    MenuItem::action("Reset Settings", '1'),
};

}  // namespace

const MenuItem *MenuTree::root() { return Root; }

uint8_t MenuTree::rootCount() { return sizeof(Root) / sizeof(Root[0]); }

}  // namespace Osd
