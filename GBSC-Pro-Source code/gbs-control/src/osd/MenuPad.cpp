#include "MenuPad.h"

namespace Osd {

const MenuCommand &MenuPad::up() const { return up_; }

const MenuCommand &MenuPad::down() const { return down_; }

const MenuCommand &MenuPad::left() const { return left_; }

const MenuCommand &MenuPad::right() const { return right_; }

}  // namespace Osd
