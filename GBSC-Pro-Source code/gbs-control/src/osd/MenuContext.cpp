#include "MenuContext.h"

namespace Osd {

MenuContext::MenuContext(Tv5725::Controls &controls, userOptions &options,
                         avOptions &av)
    : controls_(controls), options_(options), av_(av)
{
}

Tv5725::Controls &MenuContext::controls() const { return controls_; }

userOptions &MenuContext::options() const { return options_; }

avOptions &MenuContext::av() const { return av_; }

}  // namespace Osd
