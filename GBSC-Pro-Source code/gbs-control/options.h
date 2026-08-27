#ifndef _USER_H_
#include "src/tv5725/OutputMode.h"

using Ascii8 = uint8_t;

/// Output resolution requested by user, *given to* applyPresets(). Defined by
/// OutputMode, which is the one thing that resolves a preference to a raster,
/// and re-exported here because the sketch names it unqualified. An enumerator
/// added there and not listed here fails at its first use site rather than
/// silently reading as a different value.
using Tv5725::PresetPreference;
using Tv5725::Output960P;
using Tv5725::Output480P;
using Tv5725::OutputCustomized;
using Tv5725::Output720P;
using Tv5725::Output1024P;
using Tv5725::Output1080P;
using Tv5725::Output576P;
using Tv5725::OutputBypass;

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

// userOptions holds user preferences / customizations
// userOptions 保存用户偏好/自定义设置
struct userOptions
{
    // 0 - normal, 1 - x480/x576, 2 - customized, 3 - 1280x720, 4 - 1280x1024, 5 - 1920x1080,
    // 10 - bypass
    PresetPreference presetPreference;
    // INPUT_/SETTING_/TVMODE_presetPreference were here: three members the OLED
    // menu assigned and NOTHING ever read, in RAM only -- not saved, not
    // broadcast, not branched on. The enums stay; the OLED uses them as locals,
    // which is the whole of what they were doing.

    Ascii8 presetSlot;
    uint8_t enableFrameTimeLock;   //启用帧时间锁定
    uint8_t frameTimeLockMethod;  //帧时间锁定方法
    uint8_t enableAutoGain;   //启用自动增益
    uint8_t wantScanlines;    //要扫描线
    uint8_t wantOutputComponent;  //要输出组件
    uint8_t deintMode;        //非int模式
    uint8_t wantVdsLineFilter;  //想要 VdsLine过滤器
    uint8_t wantPeaking;   //峰值
    uint8_t wantTap6;
    uint8_t preferScalingRgbhv;
    uint8_t PalForce60;
    uint8_t disableExternalClockGenerator;  //禁用外部时钟生成器
    uint8_t matchPresetSource;  //匹配预置源
    uint8_t wantStepResponse;
    uint8_t enableCalibrationADC;  //启用校准 ADC
    uint8_t scanlineStrength;   //扫描线强度
};


#include "src/tv5725/OutputChoice.h"
#include "src/tv5725/DisplayClock.h"

// runTimeOptions holds system variables
struct runTimeOptions
{
    // The output resolution THIS preset load asks for, chosen in applyPresets()
    // where the detection result is still in scope and read back in
    // doPostPresetLoadSteps().
    //
    // It cannot be re-derived at the far end: rto->videoStandardInput is
    // rewritten by PresetLoad::videoStandardInputAfterLoad(), which returns 14
    // whenever scaling RGBHV is on -- so by the time the raster is solved the
    // value the choice was made from is gone.
    //
    // A choice naming no resolution -- a custom preset, whose saved bytes are
    // the mode, or bypass -- leaves the raster alone.
    Tv5725::OutputChoice outputChoice;

    // The display clock, which the engine steers from the raster it solved and
    // the frame time lock walks away from that on every correction. It lives
    // here because both reach it; Tv5725::Geometry is handed a reference.
    Tv5725::DisplayClock displayClock;
    uint16_t noSyncCounter; // is always at least 1 when checking value in syncwatcher
    // PLL648_CONTROL_01 selects PCLKIN while the external clock generator
    // drives the display, so the register stops answering what the raster asked
    // for and the real divider is stashed here.
    uint8_t presetDisplayClock;
    uint8_t presetVlineShift;
    uint8_t videoStandardInput; // 0 - unknown, 1 - NTSC like, 2 - PAL like, 3 480p NTSC, 4 576p PAL
    uint8_t phaseSP;
    uint8_t phaseADC;
    uint8_t currentLevelSOG;
    uint8_t thisSourceMaxLevelSOG;
    uint8_t syncLockFailIgnore;
    uint8_t applyPresetDoneStage;//应用预置完成阶段
    uint8_t continousStableCounter;
    uint8_t failRetryAttempts;
    uint8_t presetID;  // PresetID
    uint8_t HPLLState;
    uint8_t medResLineCount;
    uint8_t osr;
    uint8_t notRecognizedCounter;
    bool isInLowPowerMode;
    bool clampPositionIsSet;
    bool coastPositionIsSet;

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
    bool phaseIsSet;
    bool inputIsYpBpR;
    bool syncWatcherEnabled;
    bool freezeAutomation;
    bool outModeHdBypass;
    bool printInfos;
    bool sourceDisconnected;   //源断开
    bool webServerEnabled;
    bool webServerStarted;
    bool allowUpdatesOTA;
    bool enableDebugPings;
    bool autoBestHtotalEnabled;
    bool videoIsFrozen;
    bool motionAdaptiveDeinterlaceActive;
    bool deinterlaceAutoEnabled;
    bool scanlinesEnabled;
    bool boardHasPower;
    bool presetIsPalForce60;
    bool isValidForScalingRGBHV;
    bool extClockGenDetected;
    bool HdmiHoldDetection;
};
// remember adc options across presets
struct adcOptions
{

    uint8_t r_gain;
    uint8_t g_gain;
    uint8_t b_gain;
    uint8_t r_off;
    uint8_t g_off;
    uint8_t b_off;
};
#endif