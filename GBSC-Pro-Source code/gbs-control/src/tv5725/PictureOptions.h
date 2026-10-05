#ifndef TV5725_PICTURE_OPTIONS_H
#define TV5725_PICTURE_OPTIONS_H

namespace Tv5725 {

// The user's picture filters: which of the video processor's enhancement stages
// are in circuit, and whether the ADC measures its own gain.
//
// Held rather than handed in, because a mode change has to apply them and only
// the engine runs one. VideoProcessor::init() states the rest of that block and
// deliberately writes none of these -- doing so would reset the user's choice
// on every mode change. docs/osd-menu.md
class PictureOptions {
public:
    enum {
        LineFilterDefault = 0,
        PeakingDefault = 1,
        SharpnessDefault = 0,
        StepResponseDefault = 1,
        AutoGainDefault = 0,
    };

    PictureOptions();

    void setLineFilter(bool want);
    void setPeaking(bool want);
    void setSharpness(bool want);
    void setStepResponse(bool want);
    void setAutoGain(bool want);

    bool lineFilter() const;
    bool peaking() const;
    bool sharpness() const;
    bool stepResponse() const;
    bool autoGain() const;

    void adopt(bool lineFilter, bool peaking, bool sharpness,
               bool stepResponse, bool autoGain);

private:
    bool lineFilter_;
    bool peaking_;
    bool sharpness_;
    bool stepResponse_;
    bool autoGain_;
};

}  // namespace Tv5725

#endif  // TV5725_PICTURE_OPTIONS_H
