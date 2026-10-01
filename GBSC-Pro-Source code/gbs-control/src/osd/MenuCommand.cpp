#include "MenuCommand.h"

namespace Osd {

MenuCommand::Queue MenuCommand::queue() const { return queue_; }

char MenuCommand::letter() const { return letter_; }

VideoSourceSelection::Id MenuCommand::source() const
{
    return queue_ == InputSelection ? (VideoSourceSelection::Id)letter_
                                    : VideoSourceSelection::None;
}

bool MenuCommand::asked() const { return letter_ != 0; }

}  // namespace Osd
