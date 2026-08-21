#include "DisplayWindow.h"

namespace Tv5725 {

DisplayWindow::DisplayWindow() {}

DisplayWindow::DisplayWindow(const BlankingTiming &horizontal, const BlankingTiming &vertical)
    : horizontal_(horizontal), vertical_(vertical) {}

const BlankingTiming &DisplayWindow::horizontal() const { return horizontal_; }

const BlankingTiming &DisplayWindow::vertical() const { return vertical_; }

}  // namespace Tv5725
