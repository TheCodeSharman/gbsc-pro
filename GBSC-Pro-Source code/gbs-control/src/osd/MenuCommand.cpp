#include "MenuCommand.h"

namespace Osd {

MenuCommand::Queue MenuCommand::queue() const { return queue_; }

char MenuCommand::letter() const { return letter_; }

VideoSourceSelection::Id MenuCommand::source() const
{
    return queue_ == InputSelection ? (VideoSourceSelection::Id)letter_
                                    : VideoSourceSelection::None;
}

Tv5725::Nudge::Control MenuCommand::control() const
{
    return (Tv5725::Nudge::Control)letter_;
}

int8_t MenuCommand::direction() const { return direction_; }

// A nudge's control is an id and the first of them is zero, so what says a pad
// press asked for something is its direction.
bool MenuCommand::asked() const
{
    return queue_ == GeometryNudge ? direction_ != 0 : letter_ != 0;
}

}  // namespace Osd
