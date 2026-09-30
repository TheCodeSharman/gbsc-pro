#include "MenuCommand.h"

namespace Osd {

MenuCommand::Queue MenuCommand::queue() const { return queue_; }

char MenuCommand::letter() const { return letter_; }

bool MenuCommand::asked() const { return letter_ != 0; }

}  // namespace Osd
