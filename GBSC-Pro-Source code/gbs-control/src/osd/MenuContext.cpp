#include "MenuContext.h"

namespace Osd {

MenuContext::MenuContext(Tv5725::Controls &controls, userOptions &options)
    : controls_(controls), options_(options)
{
}

Tv5725::Controls &MenuContext::controls() const { return controls_; }

userOptions &MenuContext::options() const { return options_; }

}  // namespace Osd
