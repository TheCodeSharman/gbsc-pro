#include "AgreementRun.h"

namespace Tv5725 {

AgreementRun::AgreementRun(uint8_t budget) : budget_(budget), spent_(0) {}

bool AgreementRun::offer(bool agreed)
{
    if (agreed || spent_ >= budget_) {
        spent_ = 0;
        return true;
    }
    ++spent_;
    return false;
}

bool AgreementRun::awaiting() const { return spent_ > 0; }

void AgreementRun::reset() { spent_ = 0; }

}  // namespace Tv5725
