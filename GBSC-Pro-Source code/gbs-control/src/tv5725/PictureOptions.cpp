#include "PictureOptions.h"

namespace Tv5725 {

PictureOptions::PictureOptions()
    : lineFilter_(LineFilterDefault), peaking_(PeakingDefault),
      sharpness_(SharpnessDefault), stepResponse_(StepResponseDefault),
      autoGain_(AutoGainDefault),
      outputComponent_(OutputComponentDefault)
{
}

void PictureOptions::setLineFilter(bool want) { lineFilter_ = want; }
void PictureOptions::setPeaking(bool want) { peaking_ = want; }
void PictureOptions::setSharpness(bool want) { sharpness_ = want; }
void PictureOptions::setStepResponse(bool want) { stepResponse_ = want; }
void PictureOptions::setAutoGain(bool want) { autoGain_ = want; }
void PictureOptions::setOutputComponent(bool want) { outputComponent_ = want; }

bool PictureOptions::lineFilter() const { return lineFilter_; }
bool PictureOptions::peaking() const { return peaking_; }
bool PictureOptions::sharpness() const { return sharpness_; }
bool PictureOptions::stepResponse() const { return stepResponse_; }
bool PictureOptions::autoGain() const { return autoGain_; }
bool PictureOptions::outputComponent() const { return outputComponent_; }

}  // namespace Tv5725
