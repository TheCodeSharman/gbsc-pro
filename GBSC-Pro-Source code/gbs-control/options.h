#ifndef _USER_H_
#define _USER_H_
#include "src/tv5725/OutputMode.h"

using Ascii8 = uint8_t;

enum INPUT_PresetPreference : uint8_t {
  MT_RGBs   ,
  MT_RGsB   ,
  MT_VGA    ,
  MT_YPBPR    ,
  MT_SV     ,
  MT_AV     ,
};

enum SETTING_PresetPreference : uint8_t {
    // MT_7391_OFF   ,
    // MT_7391_ON    ,
    MT_7391_1X    ,
    MT_7391_2X    ,
    MT_SMOOTH_OFF ,
    MT_SMOOTH_ON  ,
    MT_COMPATIBILITY_OFF   ,
    MT_COMPATIBILITY_ON    ,
    MT_ACE_OFF   ,
    MT_ACE_ON    ,
    // MT_7391_AV    ,
    // MT_7391_SV    ,
};

enum TVMODE_PresetPreference : uint8_t {
    MT_MODE_AUTO=0,
    MT_MODE_PAL,
    MT_MODE_NTSCM,
    MT_MODE_PAL60,
    MT_MODE_NTSC443,
    MT_MODE_NTSCJ,
    MT_MODE_PALNwp,
    MT_MODE_PALMwop,
    MT_MODE_PALM,
    MT_MODE_PALCmbN,
    MT_MODE_PALCmbNwp,
    MT_MODE_SECAM,
    
};

// Fixed width, because the preferences file is a flat byte stream. The
// longest name a mode reports is nine characters.
static const uint8_t OutputResolutionBytes = 10;

// userOptions holds user preferences / customizations
// userOptions 保存用户偏好/自定义设置
struct userOptions
{
    // The output mode the user chose, as the mode names itself -- "1920x1080".
    // OutputMode::fromName() resolves it; nothing between here and the engine
    // carries a code for it.
    char outputResolution[OutputResolutionBytes];
    // INPUT_/SETTING_/TVMODE_presetPreference were here: three members the OLED
    // menu assigned and NOTHING ever read, in RAM only -- not saved, not
    // broadcast, not branched on. The enums stay; the OLED uses them as locals,
    // which is the whole of what they were doing.

    Ascii8 presetSlot;
    uint8_t enableFrameTimeLock;   //启用帧时间锁定
    uint8_t frameTimeLockMethod;  //帧时间锁定方法
    uint8_t wantScanlines;    //要扫描线
    uint8_t deintMode;        //非int模式
    uint8_t wantTap6;
    uint8_t preferScalingRgbhv;
    // Whether the board narrows the room to the source's shape. A display set
    // to its own 4:3 mode shapes what it is sent, so the two compound.
    // docs/aspect-ratio.md
    uint8_t applyAspect;
    uint8_t PalForce60;
    uint8_t disableExternalClockGenerator;  //禁用外部时钟生成器
    uint8_t enableCalibrationADC;  //启用校准 ADC
    uint8_t scanlineStrength;   //扫描线强度
};


#include "src/tv5725/DisplayClock.h"

// runTimeOptions holds system variables
struct runTimeOptions
{
    // The display clock, which the engine steers from the raster it solved and
    // the frame time lock walks away from that on every correction. It lives
    // here because both reach it; Tv5725::VideoPath is handed a reference.
    Tv5725::DisplayClock displayClock;
    uint8_t applyPresetDoneStage;//应用预置完成阶段
    bool isInLowPowerMode;

    // Whether the composite-vs-separate sync choice has been made for THIS
    // source. Same shape as the two above, and for the same reason: it is
    // expensive to establish and does not change while the source does not.
    //
    // sourceHasOwnVsync() costs ~500 ms, and applyPresets() runs on every mode
    // change -- docs/sync-type-selection.md costed calling it there and that is
    // why it had never been done. It is a property of the SOURCE and the analog
    // routing, not of the output resolution, so it is decided once and this flag
    // is what stops a mode change paying for it again. Cleared wherever the
    // other two are, which is every path that means "the source may have
    // changed".
    bool syncWatcherEnabled;
    bool freezeAutomation;
    bool printInfos;
    bool sourceDisconnected;   //源断开
    bool webServerEnabled;
    bool webServerStarted;
    bool allowUpdatesOTA;
    bool enableDebugPings;
    bool deinterlaceAutoEnabled;
    bool isValidForScalingRGBHV;
};
// The AV module's own picture, which lives behind the HC32 on the ADV7280 and
// the ADV7391. That path is WRITE-ONLY -- no conductor carries a reply -- so
// these are held here and sent, never read back.
//
// Separate from userOptions because Reset Settings wipes that one, and the AV
// module's calibration is not a scaler preference.
struct avOptions
{
    // Which broadcast standard the decoder is told to expect, per input. An
    // index into the mode table the HC32 frame carries.
    uint8_t svMode;
    uint8_t avMode;

    // The ADV7391's picture controls, 128 being the middle of each range.
    uint8_t bright;
    uint8_t contrast;
    uint8_t saturation;

    uint8_t lineDouble;
    uint8_t smooth;

    // The persisted compatibility preference the RGB inputs also write.
    uint8_t rgbCompatible;
};

// remember adc options across presets
#endif