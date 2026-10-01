#include "MenuTree.h"

#include "../../options.h"
#include "../tv5725/Aspect.h"
#include "../tv5725/Controls.h"
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

const char *aspectText(const MenuContext &context)
{
    switch (context.controls().engine().aspect().tenThousandths()) {
    case Tv5725::Aspect::FourThree:
        return "4:3";
    case Tv5725::Aspect::SixteenNine:
        return "16:9";
    case Tv5725::Aspect::FiveFour:
        return "5:4";
    default:
        return "Fill";
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

// Pass Through is absent deliberately: it is not a resolution -- OutputChoice
// cannot express it -- and the option behind it is the upscaling preference
// under System Settings.
const MenuItem Resolution[] = {
    MenuItem::action("1920x1080", 's'),
    MenuItem::action("1280x1024", 'p'),
    MenuItem::action("1280x960", 'f'),
    MenuItem::action("1280x720", 'g'),
    MenuItem::action("768x576", 'j'),
    MenuItem::action("720x480", 'h'),
};

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

const MenuItem Screen[] = {
    MenuItem::pad("Move", MovePad),
    MenuItem::pad("Scale", ScalePad),
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

const MenuItem System[] = {
    MenuItem::choice("Aspect", 'G', aspectText),
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
