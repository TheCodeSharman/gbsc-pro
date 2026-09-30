#include "MenuTree.h"

#include "../../options.h"
#include "../tv5725/Aspect.h"
#include "../tv5725/Controls.h"
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

const char *stepResponseText(const MenuContext &context)
{
    return onOff(context.options().wantStepResponse);
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

const MenuItem Picture[] = {
    MenuItem::adjust("ADC gain", MenuCommand::serial('T'), MenuCommand::user('n'),
                     MenuCommand::user('o'), autoGainText),
    MenuItem::adjust("Scanlines", MenuCommand::user('7'), MenuCommand::user('K'),
                     MenuCommand::user('K'), scanlinesText),
    MenuItem::choice("Line filter", 'm', lineFilterText),
    MenuItem::serialChoice("Peaking", 'f', peakingText),
    MenuItem::serialChoice("Step response", 'V', stepResponseText),
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
    MenuItem::submenu("Output Resolution", Resolution,
                      sizeof(Resolution) / sizeof(Resolution[0])),
    MenuItem::submenu("Picture Settings", Picture,
                      sizeof(Picture) / sizeof(Picture[0])),
    MenuItem::submenu("System Settings", System,
                      sizeof(System) / sizeof(System[0])),
    MenuItem::action("Reset Settings", '1'),
};

}  // namespace

const MenuItem *MenuTree::root() { return Root; }

uint8_t MenuTree::rootCount() { return sizeof(Root) / sizeof(Root[0]); }

}  // namespace Osd
