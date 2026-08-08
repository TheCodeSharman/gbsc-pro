#include "CaptureWindow.h"

namespace Tv5725 {

CaptureWindow::CaptureWindow() : sp_(0), st_(0) {}

CaptureWindow::CaptureWindow(uint16_t sp, uint16_t st) : sp_(sp), st_(st) {}

uint16_t CaptureWindow::sp() const { return sp_; }

uint16_t CaptureWindow::st() const { return st_; }

uint16_t CaptureWindow::width() const { return st_ > sp_ ? st_ - sp_ : 0; }

bool CaptureWindow::usable() const { return st_ > sp_; }

}  // namespace Tv5725
