/*
System
*/
extern unsigned long OledUpdataTime;

// import machine
static uint8_t Info_sate = 0;
static bool decode_flag = 0;
#define digitalRead(x) ((GPIO_REG_READ(GPIO_IN_ADDRESS) >> x) & 1)
#define DEBUG_IN_PIN D6 
// LED_BUILTIN    15
// #define LEDON     pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW)
// #define LEDOFF    pinMode(LED_BUILTIN, INPUT);  digitalWrite(LED_BUILTIN, HIGH)

// GBS_DEBUG: compile in the debug surface -- traces, dumps and the register
// endpoints. Off by default.
#ifndef GBS_DEBUG
#define GBS_DEBUG 0
#endif

// GBS_BUILD_REV: the commit the image was built from, so a unit can name its
// own firmware. "unknown" means a build that was not told, which is itself the
// answer a bisect needs.
#ifndef GBS_BUILD_REV
#define GBS_BUILD_REV "unknown"
#endif

// GBS_SAMPLING_LOG: compile in /samplinglog. Off by default even at GBS_DEBUG=1,
// because its sweep writes and latches PLLAD_MD outside the geometry engine.
#ifndef GBS_SAMPLING_LOG
#define GBS_SAMPLING_LOG 0
#endif
#if GBS_SAMPLING_LOG && !GBS_DEBUG
#error "GBS_SAMPLING_LOG needs GBS_DEBUG=1: tv5725Log expands to nothing without it"
#endif

// One line to the web
// console, format string kept in flash, and nothing at all when the flag is
// off. Expands where it is used, so SerialM does not have to exist yet here.
#if GBS_DEBUG
#define debugPrintf(fmt, ...) SerialM.printf_P(PSTR(fmt), ##__VA_ARGS__)
#else
#define debugPrintf(fmt, ...)
#endif

static inline void writeBytes(uint8_t slaveRegister, uint8_t *values, uint8_t numValues);

/*
TIM
*/
#include <PolledTimeout.h>
esp8266::polledTimeout::periodicFastUs halfPeriod(1000);

static unsigned long Tim_signal = 0;
static unsigned long Tim_sys = 0;
static unsigned long Tim_web = 0;
static unsigned long Tim_menuItem = 0;
static unsigned long Tim_Resolution = 0, Tim_Resolution_Start = 0;
#include <Wire.h>


#include "options.h"
#include "slot.h"
#include "src/net/RegisterQueue.h"
#include "src/prefs/Settings.h"
#include "gbs_types.h"   // typedef Tv5725::Tv5725 GBS, in one place
#include "src/tv5725/WriteTrace.h"
#include "src/tv5725/FramingText.h"
#include "src/tv5725/VideoPath.h"
#include "src/tv5725/FramingSaveTimer.h"
#include "src/tv5725/Controls.h"
#include "src/tv5725/ControlSteps.h"
#include "src/tv5725/PresetLoad.h"
#include "src/tv5725/FrameBuffer.h"
#include "src/tv5725/InputFormatter.h"
#include "src/tv5725/HdBypass.h"
#include "src/tv5725/SamplingClock.h"
#include "src/tv5725/ModeDetect.h"
#include "src/tv5725/Deinterlacer.h"
#include "src/tv5725/SourceMeasurement.h"
#include "src/tv5725/ColourSpace.h"
#include "src/tv5725/SyncMeasurement.h"
#include "src/tv5725/TestBus.h"
#include "src/tv5725/TestBusRateMeasurement.h"
#include "src/videosource/DetectionEntry.h"
#include "src/videosource/SourceAbsence.h"
#include "src/videosource/SourceMaintenance.h"
#include "src/videosource/SyncRecovery.h"
#include "src/videosource/FrameTimeLock.h"
#include "src/tv5725/DisplayClock.h"
#include "src/tv5725/DebugPin.h"
#include "src/clock/RateAgreement.h"
#include "src/tv5725/FrameSync.h"
#include "src/tv5725/OutputMode.h"
#include "src/tv5725/BringUp.h"
#include "src/tv5725/Chip.h"
#include "src/tv5725/VideoRoute.h"
#include "src/tv5725/RgbhvOutput.h"
#include "src/tv5725/SourceMeasurement.h"
#include "src/clock/ClockRamp.h"
#include "src/osd/Menu.h"
#include "src/osd/MenuContext.h"
#include "src/osd/MenuTree.h"
#include "src/osd/InfoScreen.h"
#include "src/osd/MuteOverlay.h"
#include "src/osd/OSD.h"
#include "src/osd/Panel.h"
#include "src/osd/VolumeOverlay.h"
#include "src/clock/ClockGen.h"
#include "src/input/HoldRamp.h"
#include "src/input/IrReceiver.h"
#include "src/videosource/VideoSourceAcquisition.h"
#include "src/videosource/VideoSourceSelection.h"
#if GBS_SAMPLING_LOG
#include "src/tv5725/SamplingLog.h"
// The sync watcher's RGBHV choices, named as they are taken. A register dump
// afterwards shows where the firmware arrived and never why.
#define SYNC_EVENT(what, lines) \
    Tv5725::SamplingLog::event(millis(), (what), (lines))
#else
#define SYNC_EVENT(what, lines) ((void)0)
#endif
#include "src/videosource/SyncSearch.h"

enum PresetID : uint8_t {
};
struct runTimeOptions rtos;
struct runTimeOptions *rto = &rtos;

// /freeze belongs to the debug surface, so without GBS_DEBUG the guards below
// compile to nothing and the endpoint is never registered.
#if GBS_DEBUG
#define AUTOMATION_FROZEN() (rto->freezeAutomation)
#else
#define AUTOMATION_FROZEN() (false)
#endif
struct userOptions uopts;
struct userOptions *uopt = &uopts;
struct avOptions avopts = {0, 0, 128, 128, 128, false, false, false};
struct avOptions *avo = &avopts;

String slotIndexMap = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~()!*:,";

char serialCommand;

char userCommand;

// An input selection asked for over HTTP, waiting for loop() to act on it.
// **THE ROUTE CANNOT DO IT ITSELF.** Selecting an input writes TV5725 registers,
// sends the AV module a frame over the UART and saves the preferences, and the
// web server serves from network-stack callbacks rather than from loop() -- so
// doing any of that in the handler touches the bus from the wrong context. The
// route parses and queues; loop() selects.
volatile uint8_t pendingInputSelection = VideoSourceSelection::None;
// NULL is nothing asked for.
const Tv5725::OutputMode *volatile pendingOutputMode = NULL;
// 0 is nothing asked for; any other value is the slot character.
volatile uint8_t pendingSlotSelection = 0;

#if GBS_SAMPLING_LOG
Tv5725::SamplingLog samplingLog;

// What /samplinglog queued, for loop() to start. Two words rather than a call:
// the route answers from a network callback, which must not touch the bus.
volatile bool pendingSamplingMonitor = false;
volatile bool pendingSamplingSweep = false;
volatile bool pendingSamplingRates = false;
volatile uint16_t pendingSamplingA = 0;
volatile uint16_t pendingSamplingB = 0;
volatile uint16_t pendingSamplingC = 0;
volatile uint32_t pendingSamplingD = 0;
#endif

#if GBS_TRACE_WRITES
// What /writetrace queued. A replay issues real bus traffic, so it belongs to
// loop() like every other write.
volatile bool pendingWriteReplay = false;
volatile uint16_t pendingWriteReplayFirst = 0;
volatile uint16_t pendingWriteReplayLast = 0;
volatile bool pendingWriteReplayGaps = false;
#endif

#if GBS_DEBUG
// What /avframe queued: the AV module frame byte, or -1 for none. The UART
// write belongs to loop() for the same reason the bus does.
volatile int16_t pendingAvFrame = -1;

// What /testbus queued, for loop() to start. The route answers from a network
// callback, which must not touch the bus.
volatile bool pendingTestBusSweep = false;
volatile uint16_t pendingTestBusMs = 25;
volatile uint8_t pendingTestBusSp = 0xff;
volatile uint8_t pendingTestBusSig = 0;
volatile uint8_t pendingTestBusIf = 0xff;

// What /sampleclock queued. Same reason: the route answers from a network
// callback and the bus belongs to loop().
volatile bool pendingCoast = false;
volatile bool pendingCoastClear = false;
volatile bool pendingCoastApply = false;
volatile uint8_t pendingCoastPre = 0;
volatile uint8_t pendingCoastPost = 0;
volatile bool pendingSampleClock = false;
volatile bool pendingSampleClockApply = false;
volatile uint16_t pendingSampleClockDivider = 0;
volatile uint8_t pendingSampleClockOversample = 0;
volatile bool pendingDividerHold = false;
volatile uint16_t pendingHeldDivider = 0;

// What /inputscale queued. The scaling-down block is ahead of the capture
// window, so a ratio re-solves every window placed in IF units.
volatile bool pendingInputScale = false;
volatile uint16_t pendingInputScaleRate = 0;

// What /framing/full queued, for the same reason.
volatile bool pendingFullFramingChange = false;
volatile bool pendingFullFraming = false;

// A reset asked for over HTTP. Without one the only way to restart the ESP is a
// flash or the mains, and neither is available to a session working remotely --
// so a boot that comes up without a source cannot be repeated to find out why.
volatile bool pendingRestart = false;
#endif

// How many output pixels the pending press asked for, or 0 for the pad's own
// step. Cleared as the command is consumed, so a press from the OSD or the
// serial port never inherits one.
int16_t serialCommandPixels;
static uint8_t lastSegment = 0xFF;

static uint16_t St;
static uint16_t Sp;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

/*
IR
*/
#include <IRremoteESP8266.h>
#include <IRutils.h>
const int kRecvPin = 2; // D4，infrared receiver PIN
IrReceiver irrecv(kRecvPin);
decode_results results;

/*
OSD
*/

float version = 1.2 ;
String full_version = String(version,1);
const char *final_version = full_version.c_str();
#define MODEOPTION_MAX 12
#define MODEOPTION_MIN 0

#define STEP 1
#define OSD_CLOSE_TIME 16000// *100            // 16 sec

// How long MUTE ON / MUTE OFF stays up. It is an acknowledgement rather than a
// screen, so it does not wait out OSD_CLOSE_TIME.
static const unsigned long MuteOverlayDwellMs = 1200;

// How often the info screen rebuilds. It reads the output frame rate, which
// times pulses rather than reading a register, so this is a cost ceiling rather
// than a refresh preference.
static const unsigned long InfoScreenRefreshMs = 1000;

// The volume last put on screen, or -1 for "nothing is". Invalidated when the
// overlay opens so that reopening at an unchanged level still draws.
static int volumeShown = -1;
#define OSD_RESOLUTION_UP_TIME 1000     // 1 sec
#define OSD_RESOLUTION_CLOSE_TIME 20000 // 20 sec
#include "OSD_TV/remote.h"
#include "OSD_TV/OSD_stv9426.h"
#include "OSD_TV/PT2257.h"

#define New_oled_menuItem_Start 153

// OSDSET
#define OSD_CROSS_TOP '7'
#define OSD_CROSS_MID '8'
#define OSD_CROSS_BOTTOM '9'


typedef enum {
    OSD_ScreenSettings = 63,
    OSD_ColorSettings = 64,
    OSD_ResetDefault = 67,

    OSD_Resolution = 62,
    OSD_Resolution_1080 = 68,
    OSD_Resolution_1024 = 69,
    OSD_Resolution_960 = 70,
    OSD_Resolution_720 = 71,
    OSD_Resolution_480 = 72,
    OSD_Resolution_576 = 73,
    OSD_Resolution_pass = 74,
    OSD_SystemSettings = 65,

    OSD_Input = New_oled_menuItem_Start + 1,
    OSD_Input_RGBs,
    OSD_Input_RBsB,
    OSD_Input_VGA,
    OSD_Input_YPBPR,
    OSD_Input_SV,
    OSD_Input_AV,
    OSD_SystemSettings_SVAVInput,
    OSD_SystemSettings_SVAVInput_DoubleLine,
    OSD_SystemSettings_SVAVInput_Smooth,
    OSD_SystemSettings_SVAVInput_Bright,
    OSD_SystemSettings_SVAVInput_contrast,
    OSD_SystemSettings_SVAVInput_saturation,
    OSD_SystemSettings_SVAVInput_default,
    OSD_SystemSettings_SVAVInput_Compatibility,
    OSD_Resolution_RetainedSettings,

} OSD_Menu;
char adl = 0;
boolean IR = 0;
uint8_t Volume = 0;
boolean MUTE_R = 0;
static int oled_menuItem = 0;
static int oled_menuItem_last = 0;
static uint8_t OLED_clear_flag = ~0;
boolean NEW_OLED_MENU = true;

static uint8_t SvModeOptionChanged, AvModeOptionChanged;
static bool SettingLineOptionChanged, SettingSmoothOptionChanged;

static uint8_t tentative = 0xfe;
// uint8_t RGBs_CompatibilityChanged;
// uint8_t RGsB_CompatibilityChanged;
// uint8_t VGA_CompatibilityChanged;






/*
OLED
*/
#include "SSD1306Wire.h"
#include "images.h"
SSD1306Wire display(0x3c, D2, D1); 
/*
Decode
*/
#include <Versatile_RotaryEncoder.h>
Versatile_RotaryEncoder *versatile_encoder;
const int pin_a = 14;   // D5 = GPIO14 (input of one direction for encoder)
const int pin_b = 13;  // D7 = GPIO13	(input of one direction for encoder)
const int pin_switch = 0; // D3 = GPIO0 


void handleRotate(int8_t rotation);

/*
OLED MENU
*/
#define USE_NEW_OLED_MENU 1
#if USE_NEW_OLED_MENU
#include "OLEDMenuImplementation.h" 
OLEDMenuManager oledMenu(&display);
// Hold a geometry key to go faster, tap it for one pixel. Shared by the Move and
// Scale screens so ramping one and then the other starts fresh, which is what
// changing your mind should do. See src/input/HoldRamp.h.
HoldRamp geometryHold;
volatile OLEDMenuNav oledNav = OLEDMenuNav::IDLE;
volatile uint8_t rotaryIsrID = 0;
#endif

uint8_t syncFound = 0;
// uint8_t InCurrent = 0;
uint8_t BriorCon = 0;
// uint8_t InputChanged = 0;
uint8_t SeleInputSource = 0;

// Set when this boot could not read the settings file. Everything running
// afterwards is on defaults that were never the user's, so nothing may write
// them back over the copy that is still on flash.
bool prefsAreSuspect = false;

// The first seconds of a boot, held in RAM and served over HTTP at /bootlog,
// and replayed to the first WebSocket client so the web console is complete.
//
// A cold start cannot be watched live. The boot trace prints at ~1.6 s, before
// WiFi is up, so only the serial cable sees it -- and the serial cable is USB,
// and USB backfeeds power, so it stops being the mains-only boot under
// diagnosis. Buffering reads it without either: power up
// with nothing attached, then `curl http://<ip>/bootlog`. Attaching serial
// changes the boot being measured.
//
// **RAM, NOT LittleFS.** What this exists to diagnose is whether the flash
// answers reads early in boot, so writing the answer to that flash would
// perturb the measurement. The cost is that the log does not survive a reset.
//
// Everything printed through SerialM lands here, not just the boot trace.
//
// Off by default: the buffer is 2 KB of globals on a unit with ~20 KB of free
// heap, and the console stops broadcasting below its heap threshold, so
// carrying it permanently costs the console.
//
//     make -C build flash BOOTLOG_BYTES=2048
#ifndef BOOTLOG_BYTES
#define BOOTLOG_BYTES 0
#endif

#if BOOTLOG_BYTES > 0
static char bootLog[BOOTLOG_BYTES];
static uint16_t bootLogLen = 0;
static bool bootLogDelivered = false;
#else
static const uint16_t bootLogLen = 0;
static bool bootLogDelivered = true;   // nothing to hand over
#endif

// Capture stops once the backlog has been handed over. This is boot history, not
// a rolling buffer -- past the first client the live stream is the record, and a
// ring buffer here would cost RAM to duplicate it.
static void bootLogAppend(const char *data, size_t len)
{
#if BOOTLOG_BYTES == 0
    (void)data;
    (void)len;
    return;
#else
    if (bootLogDelivered || len == 0) {
        return;
    }
    if (bootLogLen + len >= BOOTLOG_BYTES) {
        len = BOOTLOG_BYTES - 1 - bootLogLen; // keep the head; the tail is live anyway
    }
    if (len == 0) {
        return;
    }
    memcpy(bootLog + bootLogLen, data, len);
    bootLogLen += len;
#endif
}

/*
audio
*/
#include "src/si5351mcu.h"
Si5351mcu Si;
// The board's use of it -- detection, bring-up, every write. See
// src/clock/ClockGen.h; the driver above stays the driver.
Clock::ClockGen clockGen(Si);

/*
wifi
*/
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include <LittleFS.h>

#include <DNSServer.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h> 
#include <ArduinoOTA.h>
#include "PersWiFiManager.h"
#include "src/WebSockets.h"
#include "src/WebSocketsServer.h"

#define THIS_DEVICE_MASTER
#ifdef THIS_DEVICE_MASTER
const char *ap_ssid = "gbscontrol";


uint32_t chipId = ESP.getChipId();


String full_ssid = String(ap_ssid) + String(chipId);
const char *final_ssid = full_ssid.c_str();

const char *ap_password = "qqqqqqqq";
const char *device_hostname_full = "gbscontrol.local";
const char *device_hostname_partial = "gbscontrol"; // for MDNS
// static const char ap_info_string[] PROGMEM =
//     "(WiFi): AP mode (SSID: ";
// const char *ap_info_tail = ", pass 'qqqqqqqq'): Access 'gbscontrol.local' in your browser";
// static const char st_info_string[] PROGMEM =
//     "(WiFi): Access 'http://gbscontrol:80' or 'http://gbscontrol.local' (or device IP) in your browser";
#endif

AsyncWebServer server(80);
DNSServer dnsServer;
WebSocketsServer webSocket(81);
// AsyncWebSocket webSocket("/ws");
PersWiFiManager persWM(server, dnsServer);

// #define HAVE_PINGER_LIBRARY
#ifdef HAVE_PINGER_LIBRARY
#include <Pinger.h>
#include <PingerResponse.h>
unsigned long pingLastTime;
Pinger pinger; 
#endif


// Defined below, with the rest of the engine it is wired to.
extern FrameTimeLock frameTimeLock;

// The run-time state that says nothing about what is attached: the acquisition
// machinery's own defaults. Boot and the input handlers' reset both start from
// it, and what they believe about the SOURCE differs -- LoadDefault() comes up
// with the frame buffer frozen and boot does not -- so that half stays at the
// call sites.
static void resetRunTimeDefaults()
{
    frameTimeLock.forgiveFailures();
    rto->syncWatcherEnabled = true;
    Tv5725::Adc::choosePhaseAdc(16);
    Tv5725::Adc::choosePhaseSyncProcessor(16);
    Tv5725::Deinterlacer::disableMotionAdapt();
    rto->deinterlaceAutoEnabled = true;
    Tv5725::Deinterlacer::forgetScanlines();
    Tv5725::Deinterlacer::forgetSteering();
    Tv5725::Chip::holdPower(true);
    Tv5725::SyncMeasurement::forget();
    rto->isValidForScalingRGBHV = false;
}

static void LoadDefault()
{
    loadDefaultUserOptions();

    resetRunTimeDefaults();

    Tv5725::VideoRoute::toScaler();   
    rto->sourceDisconnected = true; 
    // rto->isInLowPowerMode = false;
    rto->applyPresetDoneStage = 0; //
    Tv5725::SyncProcessor::forgetPositions();
    Tv5725::SyncMeasurement::forget();
    Tv5725::SyncOnGreen::choose(5);        
}

void web_service(uint8_t inputStage, uint8_t segmentCurrent, uint8_t registerCurrent, uint8_t readout, uint8_t inputToogleBit);
#if GBS_DEBUG
// Declared here rather than by the .ino prototype generator, which inserts
// prototypes above the ESPAsyncWebServer.h include where the type is unknown.
static bool getHexParam(AsyncWebServerRequest *request, const char *name, long limit, long *out);
static void submitRegisterJob(AsyncWebServerRequest *request, const RegisterQueue::Job &job);
static void serviceRegisterQueue();
#endif
void Mode_Option(void);
void Mode_Option(void)
{
    // 带调试信息的参数校验
    static char max_index = sizeof(modes) / sizeof(modes[0]) - 1;
    if (SvModeOptionChanged) {
        SvModeOptionChanged = 0;


        if (avo->svMode >= 0 && avo->svMode <= max_index) {
            Send_TvMode(modes[avo->svMode]);
        } else {
            Send_TvMode(modes[1]); // 强制回退到 Auto 模式
        }
    }
    if (AvModeOptionChanged) {
        AvModeOptionChanged = 0;

        if (avo->avMode >= 0 && avo->avMode <= max_index) {
            Send_TvMode(modes[avo->avMode]);
        } else {
            Send_TvMode(modes[1]);
        }
    }
    if (SettingLineOptionChanged) {
        SettingLineOptionChanged = 0;
        Send_Line(avo->lineDouble);
    }
    if (SettingSmoothOptionChanged) {
        SettingSmoothOptionChanged = 0;
        Send_Smooth(avo->smooth);
    }
}

void ChangeSvModeOption(uint8_t num);
void ChangeSvModeOption(uint8_t num)
{
    avo->svMode = num;
    saveUserPrefs();
}
void ChangeAvModeOption(uint8_t num);
void ChangeAvModeOption(uint8_t num)
{
    avo->avMode = num;
    saveUserPrefs();
}
uint8_t getMovingAverage(uint8_t item);
uint8_t getMovingAverage(uint8_t item)
{
    static const uint8_t sz = 16;
    static uint8_t arr[sz] = {0};
    static uint8_t pos = 0;

    arr[pos] = item;
    if (pos < (sz - 1)) {
        pos++;
    } else {
        pos = 0;
    }

    uint16_t sum = 0;
    for (uint8_t i = 0; i < sz; i++) {
        sum += arr[i];
    }

    return sum >> 4;
}

// Free heap below which the console stops broadcasting, rather than risk an
// allocation failure inside broadcastTXT(). A broadcast is a few hundred bytes,
// so the threshold only has to cover that with headroom. Set it anywhere near
// the heap this chip actually has spare and the gate never opens: the socket
// accepts clients and then delivers nothing, which reads as a silent firmware
// rather than a shut gate.
static const uint32_t CONSOLE_BROADCAST_MIN_HEAP = 8000;

class SerialMirror : public Stream
{
public:
    size_t write(const uint8_t *data, size_t size)
    {
        bootLogAppend((const char *)data, size);
        if (ESP.getFreeHeap() > CONSOLE_BROADCAST_MIN_HEAP) {
            webSocket.broadcastTXT(data, size);
        }
        Serial.write(data, size);
        return size;
    }

    size_t write(const char *data, size_t size)
    {
        bootLogAppend(data, size);
        if (ESP.getFreeHeap() > CONSOLE_BROADCAST_MIN_HEAP) {
            webSocket.broadcastTXT(data, size);
        }
        Serial.write(data, size);
        return size;
    }

    size_t write(uint8_t data)
    {
        bootLogAppend((const char *)&data, 1);
        if (ESP.getFreeHeap() > CONSOLE_BROADCAST_MIN_HEAP) {
            webSocket.broadcastTXT(&data, 1);
        }
        Serial.write(data);
        return 1;
    }

    size_t write(char data)
    {
        bootLogAppend(&data, 1);
        if (ESP.getFreeHeap() > CONSOLE_BROADCAST_MIN_HEAP) {
            webSocket.broadcastTXT(&data, 1);
        }
        Serial.write(data);
        return 1;
    }

    int available()
    {
        return 0;
    }
    int read()
    {
        return -1;
    }
    int peek()
    {
        return -1;
    }
    // void flush()
    // {}
};

SerialMirror SerialM;

// The boot trace. It goes through SerialM like everything else, because that is
// what reaches the websocket console -- SerialMirror::write() appends to the
// boot log itself, so the ring still has every line and nothing is written
// twice. Printed to Serial alone these lines existed only on a cable: a
// teardown, an input selection and a detection result were all invisible to
// every instrument a session can reach remotely.
static void bootLogPrintf(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n > (int)sizeof(line) - 1) {
        n = sizeof(line) - 1; // vsnprintf reports what it wanted, not what it wrote
    }

    SerialM.write(line, (size_t)n);
}

// THE COMPOSITION ROOT. The instances are declared here, in one translation
// unit, and injected into whatever needs them.
//
// Declaration ORDER is the wiring: within a single translation unit objects are
// initialised in the order they appear, so geometry is ready before the controls
// that reference it, and the controls before the OSD that references them. It
// sits below SerialM because the controls take a reference to it.
// The input formatter, which owns the IF's counters and what their units mean.
// Held here rather than reached for, so a class that needs it says so in its
// constructor.
Tv5725::InputFormatter inputFormatter;

// What the source IS, measured off the chip. The composition root holds it and
// hands it to both the engine and the acquisition path, the way it already
// holds the display clock: the engine derives from it and does not own it.
// docs/video-source-acquisition.md
Tv5725::SourceMeasurement sourceSampling(inputFormatter);

// What the user tuned, per source. Product state rather than acquisition, and
// the root is what persists it -- holding it here is what removes the round
// trip a load and a save took through the engine. docs/video-source-acquisition.md
Tv5725::FramingTable sourceFramings;

Tv5725::VideoPath geometry(rtos.displayClock, sourceSampling, sourceFramings,
                           inputFormatter);

// The user's settings, as `key = value` text. docs/preferences-file.md
static const char SettingsFilePath[] = "/preferences.txt";

// The positional file this replaces. Deleted on the first save, so a listing
// does not show two settings files and leave the live one in doubt.
static const char LegacySettingsPath[] = "/preferencesv2.txt";

Prefs::Settings settings(uopts, avopts, geometry.colour(), Volume,
                         SeleInputSource, BriorCon);

// The framing table, in its own file. Separate from the settings because it is
// variable length and keyed, and mixing the two recreates the fragility the
// positional settings file was known for. docs/framing-presets.md
static const char FramingFilePath[] = "/framing.txt";

// A press must not write flash, so a write waits for the framing to stop
// moving. Long enough that walking a picture into place with the remote is one
// write and not forty.
static const uint32_t FramingSaveQuietMs = 15000;

// What was last written, and when the table last moved. A revision the engine
// hands out, so a quiet loop costs a comparison rather than a flash read.
static Tv5725::FramingSaveTimer framingSaves;

// Whether this boot managed to read the file. The same guard preferences carry,
// and for the same reason: a silent bad read followed by an ordinary save is
// how a table gets replaced by an empty one.
static bool framingIsSuspect = true;

// The framing a numbered slot holds, in its own file again. A slot is what the
// user chose to keep and named, where the table above is what the engine
// remembers on its own. docs/framing-presets.md

// A slot is written on an explicit save rather than on every press, so there is
// nothing to debounce -- only the same read guard.

Tv5725::Controls geometryControls(geometry, SerialM);

// The described menu, which is the only menu. /menu and the remote both drive
// it, and it draws only while the two overlays that are not menu rows -- the
// volume bar and the information screen -- are closed. docs/osd-menu.md
static Osd::MenuContext menuContext(geometryControls, uopts, avopts);
static Osd::Menu describedMenu(Osd::MenuTree::root(), Osd::MenuTree::rootCount(),
                               Osd::OSD::renderer(), menuContext);

// The panel's end of Osd::Panel, as OSD_parameters() is the overlay's. The
// chain set the colour, the alignment and the font at every branch it drew.
static void panelClear()
{
    display.clear();
    display.setColor(OLEDDISPLAY_COLOR::WHITE);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
}

static void panelLine(uint8_t y, const char *text)
{
    display.drawString(1, y, text);
}

static void panelFlush() { display.display(); }

// A pad press, queued for loop() like every other: an arrow arrives from a
// network callback on /menu and from the IR handler on the remote, and the
// engine is reached from one place. Zero steps is nothing pending.
static Tv5725::Nudge::Control pendingNudge = Tv5725::Nudge::HorizontalPan;
static int16_t pendingNudgeSteps = 0;

// The same shape for an adjustable value: which one, and how many counts.
static Osd::Tune::Control pendingTune = Osd::Tune::Red;
static int16_t pendingTuneSteps = 0;

// One action per option: the menu asks for a letter and the letter goes to the
// handler that already serves /uc? and /sc?. A pad asks for a control and a
// direction instead, because a tap is one capture granule and the letters are
// stated in output pixels.
// Whether anything the menu asked for is still waiting to be acted on. What a
// row says is read from whoever holds the value, so the page cannot be drawn
// finally until every queue the press filled has drained.
static bool menuCommandPending()
{
    return userCommand != '@' || serialCommand != '@' || pendingNudgeSteps != 0
           || pendingTuneSteps != 0
           || pendingInputSelection != VideoSourceSelection::None;
}

static void queueMenuCommand(const Osd::MenuCommand &asked, int16_t steps = 1)
{
    if (!asked.asked())
        return;
    switch (asked.queue()) {
    case Osd::MenuCommand::InputSelection:
        pendingInputSelection = asked.source();
        break;
    case Osd::MenuCommand::GeometryNudge:
        pendingNudge = asked.control();
        pendingNudgeSteps = (int16_t)(asked.direction() * steps);
        break;
    case Osd::MenuCommand::ValueTune:
        pendingTune = asked.tuned();
        pendingTuneSteps = (int16_t)(asked.direction() * steps);
        break;
    case Osd::MenuCommand::UserCommand:
        userCommand = asked.letter();
        break;
    case Osd::MenuCommand::SerialCommand:
        serialCommand = asked.letter();
        break;
    }
}

static void handleRemoteKey();

// Only the seven keys the menu answers; anything else is handed to the handler
// that owns Volume, Mute and Info. decode() is one-shot, so a frame this reads
// and does not forward is a key that does nothing.
static void pressDescribedMenuFromRemote()
{
    if (!irrecv.decode(&results))
        return;

    // WHICHEVER IS ON THE SCREEN OWNS THE REMOTE. The described menu answers
    // Exit whether it is open or not, so with an overlay up it ate the one key
    // that dismisses one and left no way out but the sixteen-second timeout.
    // Returned without resume(), so the overlay's own handler decodes the same
    // frame -- decode() answers it again until something resumes.
    if (oled_menuItem != 0)
        return;

    // A held key sends repeat frames rather than the code, and only a pad acts
    // on one: the ramp is what makes a held arrow go faster, where a level that
    // scrolled on repeat is not what the remote does today.
    const uint32_t frame = describedMenu.isAdjusting()
                               ? geometryHold.resolve(results.value, millis())
                               : results.value;

    bool known = true;
    Osd::Menu::Key key = Osd::Menu::KeyMenu;
    switch (frame) {
    case IRKeyUp:
        key = Osd::Menu::KeyUp;
        break;
    case IRKeyDown:
        key = Osd::Menu::KeyDown;
        break;
    case IRKeyLeft:
        key = Osd::Menu::KeyLeft;
        break;
    case IRKeyRight:
        key = Osd::Menu::KeyRight;
        break;
    case IRKeyOk:
        key = Osd::Menu::KeyOk;
        break;
    case IRKeyMenu:
        key = Osd::Menu::KeyMenu;
        break;
    case IRKeyExit:
        key = Osd::Menu::KeyExit;
        break;
    default:
        known = false;
        break;
    }

    if (known) {
        const Osd::MenuCommand asked = describedMenu.press(key);
        queueMenuCommand(asked,
                         asked.queue() == Osd::MenuCommand::GeometryNudge
                                 || asked.queue() == Osd::MenuCommand::ValueTune
                             ? geometryHold.multiplierFor(frame, millis())
                             : 1);
    } else {
        handleRemoteKey();
    }
    irrecv.resume();
}


// The acquisition path, which owns the tick loop() used to hand the engine
// directly. It calls down for the scaler's share; the escalation, the input
// policy and the no-signal report move into it. docs/video-source-acquisition.md
VideoSourceAcquisition inputAcquisition(sourceSampling, geometry);
SourceMaintenance sourceMaintenance;

// The only thing on the board that steers the output frame time towards the
// source's, and the gate that decides when it may.
Tv5725::FrameSync frameSync(rtos.displayClock);
FrameTimeLock frameTimeLock(frameSync, inputAcquisition, geometry, sourceSampling);

void externalClockGenResetClock()
{
    if (!rto->displayClock.driving()) {
        return;
    }
    debugPrintf("externalClockGenResetClock()\n");

    // The engine steers. It chose the seed when it solved the raster and holds
    // it, because loop() stashes the divider and parks
    // DisplayClock::ExternalPclkIn in PLL648_CONTROL_01 -- so the register
    // stops answering what the raster asked for. The paths that solve no raster
    // adopt it through VideoPath::setOutputMode(ModeBypass).
    Tv5725::DisplayClock &displayClock = rto->displayClock;
    uint32_t steered = displayClock.reset();

    bootLogPrintf("CLOCK: s0_41=0x%02x -> %lu Hz t=%lums\n",
                  displayClock.seed(), (unsigned long)steered,
                  (unsigned long)millis());

    if (steered != displayClock.hz()) {
        SerialM.printf_P(PSTR("extClockGen: display clock 0x%02x unmapped, using %luMHz\n"),
                         displayClock.seed(), (unsigned long)(steered / 1000000));
    }

    frameSync.clearFrequency();
}

void externalClockGenDetectAndInitialize()
{

    if (uopt->disableExternalClockGenerator) {
        rto->displayClock.detach();
        rto->displayClock.assumeHz(Tv5725::DisplayClock::FallbackHz);
        return;
    }

    if (!rto->displayClock.attach(clockGen, Tv5725::DisplayClock::FallbackHz)) {
        bootLogPrintf("CLOCKGEN: detect FAILED t=%lums (no external display clock)\n",
                      (unsigned long)millis());
        return;
    }

    bootLogPrintf("CLOCKGEN: detected, begin(%lu) t=%lums\n",
                  (unsigned long)rto->displayClock.hzNow(),
                  (unsigned long)millis());
}

static inline void writeOneByte(uint8_t slaveRegister, uint8_t value)
{
    writeBytes(slaveRegister, &value, 1);
}

static inline void writeBytes(uint8_t slaveRegister, uint8_t *values, uint8_t numValues)
{
    if (slaveRegister == 0xF0 && numValues == 1) {
        lastSegment = *values;
    } else
        GBS::write(lastSegment, slaveRegister, values, numValues);
}

// Which connector the source is on, which the input selection knows without the
// classifier. The standard byte carried this as its top value because Mode
// Detect names an RGBHV source nothing, and that value was only ever reached for
// the three inputs sharing the RGB port -- so the selection is the same fact,
// known earlier and without a detection pass. docs/video-source-acquisition.md
bool sourceIsRgbhv()
{
    return VideoSourceSelection::isRgbhv(VideoSourceSelection::selected());
}
bool scalingRgbhv() { return sourceIsRgbhv() && Tv5725::RgbhvOutput::isScaling(); }
bool rgbhvBypass() { return sourceIsRgbhv() && !Tv5725::RgbhvOutput::isScaling(); }

// Whether the VDS carries the video. Bypass hands the source's own timing to
// the encoder, so there is no output raster of ours to steer and nothing for
// the frame time lock to arm against.
//
// Whether the source runs a 15 kHz line. One reader, on every path: the held
// rate survives a bypass switch, so bypass is not a special case.
// docs/video-source-acquisition.md
static boolean sourceLowLineRate()
{
    return inputAcquisition.sourceLowLineRate();
}

// Bypass hands the source's OWN timing to the encoder, so it only works where
// the display can show that timing. A 15 kHz source bypassed produces no signal
// at all, which reads as the scaler having failed rather than as the television
// refusing the mode -- so the request is refused here instead.
// docs/rgbhv-bypass-trap.md
// Whether bypass would reach the panel. Asked at EVERY entry, not only at the
// serial command: bypass hands the source's own timing to the encoder, and a
// rate the display refuses puts torn content on the panel that reads as a
// broken scaler rather than as a refused mode. docs/rgbhv-bypass-trap.md
static boolean bypassCanBeDisplayed()
{
    return Tv5725::HdBypass::suitsLineRate(sourceSampling.lineRateHz());
}

// A 15 kHz line whose vertical interval carries equalisation and serration
// pulses. The rate alone does not say so, and the coast settings below break the
// horizontal count on a source that has none.
// docs/investigations/serrated-sync-is-not-line-rate.md
static boolean sourceHasSerratedSync()
{
    return sourceSampling.hasSerratedSync();
}

void zeroAll()
{
    writeOneByte(0xF0, 0);
    writeOneByte(0x46, 0x00);
    writeOneByte(0x47, 0x00);

    for (int y = 0; y < 6; y++) {
        writeOneByte(0xF0, (uint8_t)y);
        for (int z = 0; z < 16; z++) {
            uint8_t bank[16];
            for (int w = 0; w < 16; w++) {
                bank[w] = 0;
            }
            writeBytes(z * 16, bank, 16);
        }
    }
}

// The preset id, as the tables carried it in their s1_2B byte: low nibble the
// resolution, high nibble the source standard.
// The output mode the user chose. Nothing qualifies it: it names a height and
// the source does not get a say. A stored name no mode answers to -- an older
// file, or one written by a build with a mode this one lacks -- falls back
// rather than leaving the engine with no raster to solve.
static const Tv5725::OutputMode *chosenOutputMode()
{
  const Tv5725::OutputMode *mode =
      Tv5725::OutputMode::fromName(uopt->outputResolution);
  return mode != NULL ? mode : &Tv5725::Mode1080p;
}

void chooseOutputMode(const Tv5725::OutputMode *mode)
{
  strncpy(uopt->outputResolution, mode->name(), OutputResolutionBytes - 1);
  uopt->outputResolution[OutputResolutionBytes - 1] = '\0';
}

// A preset load: the mode state a load decides, and nothing else. Every
// register is computed afterwards, from `choice` -- the output resolution this
// load asks for, remembered for doPostPresetLoadSteps(), which hands it to the
// engine to resolve and solve the raster from.
//
// s1_2B and s1_2C are cleared here because nothing else clears them and they
// latch across loads.
void loadComputedPreset(const Tv5725::OutputMode *chosen)
{
  // The engine is told the choice HERE, by the call whose job that is. It used
  // to arrive as an argument to the source event further down, which is how a
  // source event came to carry output state.
  inputAcquisition.setOutputResolution(chosen);

  // The load rewrites the scanline stages, so whatever was applied is gone.
  Tv5725::Deinterlacer::forgetScanlines();
  Tv5725::Deinterlacer::forgetSteering();
  Tv5725::PresetLoad::forgetScalingRgbhv();

  frameSync.cleanup();

  Tv5725::VideoRoute::toScaler();

  // Which connector is live is held rather than read back: Adc::selectInput()
  // records what it wrote and is the only writer of ADC_INPUT_SEL.

  if (rto->isValidForScalingRGBHV)
  {
    Tv5725::RgbhvOutput::chooseScaling();
    Tv5725::PresetLoad::rememberScalingRgbhv();
  }
}

// The user's switch, shared by both commands that reach it. A toggle has to
// land somewhere defined in BOTH directions: switched on, the reset drops a
// correction measured against the state before it and the lock re-arms from
// the source in front of it; switched off, the reset takes the last correction
// back out of the raster rather than leaving it there for ever.
void toggleFrameTimeLock(bool persist)
{
    uopt->enableFrameTimeLock = !uopt->enableFrameTimeLock;
    if (persist) {
        saveUserPrefs();
    }
    frameSync.reset(uopt->frameTimeLockMethod);
}

// The ADC input the user chose, or the RGB pins when nothing is chosen -- which
// is where a sweep starts looking.
static uint8_t selectedAdcInput()
{
    const VideoSourceSelection::Id chosen = VideoSourceSelection::selected();
    if (chosen == VideoSourceSelection::None)
        return 1;
    return VideoSourceSelection::settingsFor(chosen).adcInputSel;
}

void setResetParameters()
{
    rto->applyPresetDoneStage = 0;
    rto->sourceDisconnected = true; 
    Tv5725::VideoRoute::toScaler();       
    Tv5725::SyncProcessor::forgetPositions();
    Tv5725::SyncMeasurement::forget();
    Tv5725::Adc::forgetPhase();

    rto->isInLowPowerMode = false;  
    Tv5725::SyncOnGreen::choose(5);       
    Tv5725::Deinterlacer::disableMotionAdapt();
    Tv5725::Deinterlacer::forgetScanlines();
    Tv5725::Deinterlacer::forgetSteering();
    rto->isValidForScalingRGBHV = false;          

    Tv5725::Adc::forgetGain();

    Tv5725::PresetLoad::forgetScalingRgbhv();

    // The reference line WHOLE, path registers included. The scan and the line
    // it is sized for are one setting, and the reference divider cannot be
    // represented undoubled -- so leaving the previous source's scan beside this
    // counter is what the block then times the arriving field rate through.
    inputFormatter.applyScan(Tv5725::Adc::BringUpDivider,
                             Tv5725::Adc::BringUpLineDoubled,
                             Tv5725::Adc::inputIsComponent());
    inputFormatter.writeReferenceVerticalBlank();

    frameSync.cleanup();

    GBS::OUT_SYNC_CNTRL::write(0);
    GBS::DAC_RGBS_PWDNZ::write(0);
    Tv5725::Adc::applyReferenceTrim();
    GBS::ADC_CLK_PA::write(0);
    GBS::ADC_SOGEN::write(1); 
    GBS::SP_SOG_MODE::write(1);

    // The chosen input, not a literal. This runs from low power and the RGBHV
    // watchdog as well as from boot, and only boot follows it with
    // applySavedInputSource() -- so a literal drops a component selection onto
    // the RGB pins with nothing to put it back until the next reboot.
    Tv5725::Adc::selectInput(selectedAdcInput());
    GBS::ADC_POWDZ::write(1);
    Tv5725::SyncOnGreen::putInForce();
    Tv5725::BringUp::holdAllBlocks();
    GBS::DAC_RGBS_PWDNZ::write(0);
    GBS::PLL648_CONTROL_01::write(0x00);
    GBS::IF_SEL_ADC_SYNC::write(1);
    GBS::PLLAD_VCORST::write(1);
    GBS::PLL_ADS::write(1);
    GBS::PLL_CKIS::write(0);
    GBS::PLL_MS::write(2);
    Tv5725::Chip::padsToResetState();
    Tv5725::HdBypass::init();
    Tv5725::ModeDetect::init();
    Tv5725::Adc::applyOffset(Tv5725::Adc::NeutralOffset, Tv5725::Adc::NeutralOffset,
                             Tv5725::Adc::NeutralOffset);
    GBS::SP_PRE_COAST::write(9);
    GBS::SP_POST_COAST::write(18);  
    GBS::SP_NO_COAST_REG::write(0); 
    Tv5725::SyncProcessor::applyDefaultClampWindow();
    GBS::SP_SOG_SRC_SEL::write(0);  
    Tv5725::SyncProcessor::selectExternalSync(0);
    Tv5725::SyncProcessor::holdClamp();
    Tv5725::Adc::applyResetParameters();
    resetPLL();
    delay(2);
    Tv5725::Adc::restartPll();
    Tv5725::SyncProcessor::forgetPositions();
    GBS::PLL_VCORST::write(1);
    Tv5725::Adc::holdPllInReset();

    GBS::SFTRST_IF_RSTZ::write(1);
    GBS::SFTRST_DEINT_RSTZ::write(0);
    GBS::SFTRST_MEM_FF_RSTZ::write(0);
    GBS::SFTRST_MEM_RSTZ::write(0);
    GBS::SFTRST_FIFO_RSTZ::write(0);
    GBS::SFTRST_OSD_RSTZ::write(0);
    GBS::SFTRST_VDS_RSTZ::write(1);
    GBS::SFTRST_DEC_RSTZ::write(1);
    GBS::SFTRST_MODE_RSTZ::write(1);
    GBS::SFTRST_SYNC_RSTZ::write(1);
    Tv5725::HdBypass::hold();
    GBS::SFTRST_INT_RSTZ::write(1);
    Tv5725::Interrupts::enableEverySource();
    Tv5725::Interrupts::acknowledgeAll();
    Tv5725::SyncProcessor::forgetPositions();
    Tv5725::SyncMeasurement::forget();
    Tv5725::Adc::forgetPhase();
    serialCommand = '@';
    userCommand = '@';
}

void applyComponentColorMixing()
{
    GBS::VDS_UCOS_GAIN::write(0x19);
    GBS::VDS_VCOS_GAIN::write(0x19);

    geometry.colour().restFor(Tv5725::ColourBalance::ComponentOutput);
    geometry.colour().apply();
}

void toggleIfAutoOffset()
{
    if (GBS::IF_AUTO_OFST_EN::read() == 0) {

        Tv5725::Adc::applyOffset(0x40, 0x42, 0x40);

        GBS::IF_AUTO_OFST_EN::write(1);
        GBS::IF_AUTO_OFST_PRD::write(0);
    } else {
        Tv5725::Adc::applyHeldOffset();

        GBS::IF_AUTO_OFST_EN::write(0);
        GBS::IF_AUTO_OFST_PRD::write(0);
    }
}

// The ADV7391's picture controls stop at the ends of their range, as the colour
// balance does: a wrap takes a bright picture dark.
static uint8_t steppedAvValue(uint8_t from, int16_t steps)
{
    const int32_t wanted = (int32_t)from + steps;
    if (wanted > 254)
        return 254;
    return wanted < 0 ? 0 : (uint8_t)wanted;
}

// Which class holds the value a row named, and what moving it costs. The two
// command letters and the two id surfaces all land in loop(); this is the one
// for a named value. docs/osd-menu.md
void applyTune(Osd::Tune::Control control, int16_t steps)
{
    switch (control) {
    case Osd::Tune::Red:
        geometry.colour().nudgeRed(steps);
        break;
    case Osd::Tune::Green:
        geometry.colour().nudgeGreen(steps);
        break;
    case Osd::Tune::Blue:
        geometry.colour().nudgeBlue(steps);
        break;
    case Osd::Tune::LumaGain:
        geometry.colour().nudgeLumaGain(steps);
        break;

    // The AV module's, which is a different owner and a write-only path.
    case Osd::Tune::Brightness:
        avo->bright = steppedAvValue(avo->bright, steps);
        applyAvPicture();
        return;
    case Osd::Tune::Contrast:
        avo->contrast = steppedAvValue(avo->contrast, steps);
        applyAvPicture();
        return;
    case Osd::Tune::Saturation:
        avo->saturation = steppedAvValue(avo->saturation, steps);
        applyAvPicture();
        return;
    case Osd::Tune::Format:
        stepAvFormat(steps);
        return;
    }
    applyColourBalance();
}

// Every press that moves the balance ends here: the four registers are
// ColourBalance's and it writes them from what it holds.
void applyColourBalance()
{
    geometry.colour().apply();
    debugPrintf("colour: R %u G %u B %u luma %u\n", geometry.colour().red(),
                geometry.colour().green(), geometry.colour().blue(),
                geometry.colour().lumaGain());
}

// The colour path the SELECTED INPUT is due. Detection used to key this off
// which sync it had just found, which is the same answer read from a proxy:
// Adc::selectInput() records what it put the mux on, and every route that moves
// the mux goes through it.
void applyColourPath()
{
    if (Tv5725::Adc::inputIsComponent())
        Tv5725::ColourSpace::applyYuv(geometry.colour());
    else
        Tv5725::ColourSpace::applyRgb(geometry.colour());

    if (uopt->wantOutputComponent)
        applyComponentColorMixing();
}

// What the sync processor reports about the source's sync edges, by name. The
// polarity bits are only meaningful beside their ACT bit, which is why the pair
// travels together.
static Tv5725::HdBypass::SourceSyncEdges sourceSyncEdges()
{
    Tv5725::HdBypass::SourceSyncEdges edges;
    edges.hsyncFound = GBS::STATUS_SYNC_PROC_HSACT::read() == 1;
    edges.hsyncPositive = GBS::STATUS_SYNC_PROC_HSPOL::read() == 1;
    edges.vsyncFound = GBS::STATUS_SYNC_PROC_VSACT::read() == 1;
    edges.vsyncPositive = GBS::STATUS_SYNC_PROC_VSPOL::read() == 1;
    return edges;
}

void prepareSyncProcessor()
{
    Tv5725::SyncProcessor::prepare(Tv5725::SyncMeasurement::isCsync(),
                                   sourceHasSerratedSync(),
                                   rgbhvBypass()
                                       || Tv5725::PresetLoad::scalingRgbhvInForce());
}

void goLowPowerWithInputDetection()
{
    // The dark-boot state, recorded at the moment it is entered. This powers the
    // DAC down and setResetParameters() zeroes the chip, which is the register
    // signature of a boot that comes up with no signal. If this line is in the
    // boot log, the unit is not failing to drive the display -- it has decided
    // nothing is plugged in.
    bootLogPrintf("LOWPOWER: entered at t=%lums (no sync found)\n",
        (unsigned long)millis());

    GBS::OUT_SYNC_CNTRL::write(0);
    GBS::DAC_RGBS_PWDNZ::write(0);

    setResetParameters();
    prepareSyncProcessor(); 
    delay(100);
    rto->isInLowPowerMode = true;
}

static void feedWatchdog() { ESP.wdtFeed(); }

static uint32_t millisNow() { return (uint32_t)millis(); }

static void pumpWiFi() { handleWiFi(0); }

// What the engine probes with. The connector settles the sync type on every
// input but VGA, and measuring one that is already settled gets it wrong:
// probing YPbPr answers "own vsync", which puts a sync-on-luma source on the
// separate-sync path where it never locks. With nothing chosen there is no
// connector to ask, so the measurement stands.
boolean syncTypeHasOwnVsync()
{
    const VideoSourceSelection::Id id = VideoSourceSelection::selected();
    if (VideoSourceSelection::chosen(id) && !VideoSourceSelection::syncTypeMustBeMeasured(id))
        return false;
    return sourceHasOwnVsync();
}

boolean sourceHasOwnVsync()
{
    return Tv5725::SyncMeasurement::hasOwnVsync(millisNow);
}

// Point both halves of the input path at the input the user last chose, so
// detection starts where it worked last time instead of wherever the muxes
// happen to be sitting. Without this, which input comes up is decided by
// detectAndSwitchToActiveInput()'s sweep, which alternates 0/1 from wherever it
// started.
//
// **KEYED ON THE SELECTION, NOT `SeleInputSource`.** It carries all six inputs;
// `SeleInputSource` carries three, so a restore keyed on it sends RGsB the RGBs
// frame, S-Video and composite the YPbPr frame, and VGA a frame without the low
// nibble that raises asw_01.
//
// **THE COMMENTED-OUT LINES BESIDE THE SeleInputSource LOAD ARE NOT USABLE.**
// Each calls f.read() again, so restoring them consumes two bytes the
// preferences file does not contain and shifts every field after it.
// ADC_INPUT_SEL belongs to the user once an input has been chosen. Detection's
// sweeps exist to find a source when nobody has said which one, and a unit with
// something live on the other connector otherwise walks to it: the input in use
// becomes whichever one happens to have sync rather than the one asked for.
//
// The connector needs no exception. RGBs, RGsB and VGA read one set of pins and
// YPbPr, S-Video and composite the other, so searching the sync variants of a
// chosen connector never moves this register -- only crossing to the other one
// does, and that is what a choice refuses.
static bool detectionMayChangeInput()
{
    return !VideoSourceSelection::chosen(VideoSourceSelection::selected());
}

void applySavedInputSource()
{
    const VideoSourceSelection::Id saved = VideoSourceSelection::selected();
    if (saved == VideoSourceSelection::None) {
        // Nothing chosen, so leave the muxes alone and let detection sweep.
        // Only ADC_INPUT_SEL 0 and 1 carry video -- 2 is written solely by
        // calibrateAdcOffset() as a calibration reference -- which is what
        // makes that 0/1 sweep complete.
        bootLogPrintf("INPUT: nothing stored, selection=%u t=%lums\n",
                      (unsigned)VideoSourceSelection::selected(), (unsigned long)millis());
        return;
    }

    // The same sequence every other caller gets. Applying the registers and
    // sending the frame is not it: the reference sampling clock, the scan its
    // line implies and the sync processor reset belong to a selection too, and
    // without them the first measurement of the arriving source is taken
    // through whatever divider the chip was left holding.
    restoreInputSelection(saved);

    const VideoSourceSelection::Settings settings = VideoSourceSelection::settingsFor(saved);
    bootLogPrintf("INPUT: %s frame=0x%02x ADC_INPUT_SEL=%u t=%lums\n",
                  VideoSourceSelection::name(saved), (unsigned)settings.frame,
                  (unsigned)GBS::ADC_INPUT_SEL::read(), (unsigned long)millis());
}

// SyncSearch repeats the SeleInputSource values rather than including
// OLEDMenuImplementation.h, which would pull in Arduino and cost it its host
// test. This is where both are visible, so this is where the copy is checked.
static_assert(SyncSearch::SourceRgbs == S_RGBs, "SyncSearch::SourceRgbs drifted from S_RGBs");
static_assert(SyncSearch::SourceVga == S_VGA, "SyncSearch::SourceVga drifted from S_VGA");
static_assert(SyncSearch::SourceYuv == S_YUV, "SyncSearch::SourceYuv drifted from S_YUV");

// VideoSourceSelection::Id IS the stored byte, which is what lets the boot restore
// reconstruct all six. Nothing else checks the two spellings agree.
static_assert(VideoSourceSelection::Rgbs == InfoRGBs, "VideoSourceSelection::Rgbs drifted from InfoRGBs");
static_assert(VideoSourceSelection::RgsB == InfoRGsB, "VideoSourceSelection::RgsB drifted from InfoRGsB");
static_assert(VideoSourceSelection::Vga == InfoVGA, "VideoSourceSelection::Vga drifted from InfoVGA");
static_assert(VideoSourceSelection::Ypbpr == InfoYUV, "VideoSourceSelection::Ypbpr drifted from InfoYUV");
static_assert(VideoSourceSelection::SVideo == InfoSV, "VideoSourceSelection::SVideo drifted from InfoSV");
static_assert(VideoSourceSelection::Composite == InfoAV, "VideoSourceSelection::Composite drifted from InfoAV");

// How long detection waits for the sync processor to start counting before it
// reports the source as found. The boots that work have a count within about
// 100 ms of the preset load; this is several times that, and it is only spent
// on a boot that would otherwise never acquire at all.
static const unsigned long DetectCountWaitMs = 600;

SourceAbsence sourceAbsence;

uint8_t detectAndSwitchToActiveInput()
{                                      // if any
    // First few passes only: the boot log is 2 KB and this runs forever.
    static uint8_t traceLeft = 4;
    if (traceLeft > 0) {
        --traceLeft;
        bootLogPrintf("DETECT: enter t=%lums ADCsel=%u S16=0x%02x srcVT=%u "
                      "HPERIOD=%u\n",
                      (unsigned long)millis(), (unsigned)GBS::ADC_INPUT_SEL::read(),
                      (unsigned)GBS::read(0x00, 0x16),
                      (unsigned)GBS::STATUS_SYNC_PROC_VTOTAL::read(),
                      (unsigned)GBS::HPERIOD_IF::read());
    }
    // Frozen: docs/gbs-control-debug-interface.md
    if (AUTOMATION_FROZEN()) {
        return 0;
    }
    uint8_t currentInput = GBS::ADC_INPUT_SEL::read();
    SYNC_EVENT("det enter", currentInput);
    unsigned long timeout = millis();
    while (true) {
        delay(10);
        handleWiFi(0);

        // The test bus rather than STATUS_SYNC_PROC_HSACT: that bit rails in
        // both directions and says nothing here.
        // docs/known-issues.md, "STATUS_SYNC_PROC_HSACT saturates"
        const bool present = Tv5725::SyncProcessor::signalPresent();
        SYNC_EVENT("det hsact", present ? 1 : 0);

        const DetectionEntry::Step step =
            DetectionEntry::stepAt(present, (uint32_t)(millis() - timeout));
        if (step == DetectionEntry::GiveUp)
            break;
        if (step == DetectionEntry::Wait)
            continue;

        {
            currentInput = GBS::ADC_INPUT_SEL::read();

            if ((currentInput == 1 && Info_sate == 0) && (SeleInputSource == S_VGA || SeleInputSource == S_RGBs)) // 20240919
            {                                                                                                     // RGBS or RGBHV
                SYNC_EVENT("det rgb branch", SeleInputSource);
                boolean vsyncActive = 0;
                Tv5725::SyncOnGreen::choose(13); //
                Tv5725::SyncOnGreen::putInForce();

                unsigned long timeOutStart = millis();
                // vsync test
                while (!vsyncActive && ((millis() - timeOutStart) < 360)) {
                    vsyncActive = GBS::STATUS_SYNC_PROC_VSACT::read();
                    handleWiFi(0); // wifi stack
                    delay(1);
                }
                const unsigned long vsyncWaitMs = millis() - timeOutStart;


                if (Info_sate == 0 &&
                    SyncSearch::searchFor(SeleInputSource, vsyncActive) == SyncSearch::VsyncPresent) {
                    // SerialMprintln(F("VSync: present"));
                    GBS::MD_SEL_VGA60::write(1); 
                    boolean hsyncActive = 0;

                    timeOutStart = millis();
                    const unsigned long hsyncWaitStart = millis();
                    while (!hsyncActive && millis() - timeOutStart < 400) {
                        hsyncActive = GBS::STATUS_SYNC_PROC_HSACT::read();
                        handleWiFi(0); // wifi stack
                        delay(1);
                    }
                    const unsigned long hsyncWaitMs = millis() - hsyncWaitStart;

                    if (hsyncActive) {
                        ; // SerialMprint(F("HSync: present"));
                        Tv5725::SyncProcessor::setHsyncOverflowProtect(true);
                        delay(120);

                        short decodeSuccess = 0;
                        for (int i = 0; i < 3; i++) {
                            
                            Tv5725::SyncMeasurement::set(1); // temporary for test
                            float sfr = Tv5725::TestBusRateMeasurement::sourceFieldRateHz(true);
                            Tv5725::SyncMeasurement::set(0); // undo
                            if (sfr > 40.0f)
                                decodeSuccess++; 
                        }

                        // The loop above only asks whether assuming composite sync
                        // yields a plausible field rate, and every source above 40 Hz
                        // says yes — so it can never conclude "separate sync" on its
                        // own. A V sync line of its own overrules it.
                        boolean ownVsync = syncTypeHasOwnVsync();

                        if (decodeSuccess >= 2 && !ownVsync) {
                            // SerialMprintln(F(" (with CSync)"));
                            GBS::SP_PRE_COAST::write(0x10); 
                            delay(40);
                            Tv5725::SyncMeasurement::set(true);
                        } else {
                            // SerialMprintln();
                            Tv5725::SyncMeasurement::set(false); 
                        }
                        debugPrintf("sync type: %d/3 field rate probes plausible, own V sync %s -> %s\n",
                            decodeSuccess, ownVsync ? "yes" : "no",
                            Tv5725::SyncMeasurement::isCsync() ? "csync" : "separate H/V");

                        Tv5725::RgbhvOutput::chooseBypass();
                        const unsigned long presetsAt = millis();
                        applyPresets();
                        delay(100);

                        // **FOUND IS TWO STATUS BITS; THE ENGINE NEEDS A COUNT.**
                        // Concluding here while the sync processor counts nothing
                        // commits the whole boot to a state it cannot measure,
                        // and the recovery ladder cannot escalate out of that --
                        // ReprobeSyncType succeeds on any source with its own V
                        // sync and restarts the run, so the rungs past it never
                        // fire. Measured over six restarts, the count at this
                        // moment decides it every time: zero never acquires,
                        // non-zero always does.
                        //
                        // So wait for one, and report not-found without it. The
                        // caller retries, which is what recovers this -- where
                        // returning found does not, for the life of the boot.
                        // docs/investigations/a-boot-that-detects-too-early-can-never-recover.md
                        const unsigned long countFrom = millis();
                        while (!Tv5725::VideoSignal::countIsSource(
                                   Tv5725::SyncProcessor::lineCount())
                               && millis() - countFrom < DetectCountWaitMs)
                            delay(2);
                        const unsigned countWaited = (unsigned)(millis() - countFrom);
                        const uint16_t counted = Tv5725::SyncProcessor::lineCount();

                        bootLogPrintf("DETECT: found t=%lums vsyncWait=%lums "
                                      "hsyncWait=%lums presets=%lums count=%ums "
                                      "VT=%u HT=%u "
                                      "HPERIOD=%u lock=%u SOG=%u\n",
                                      (unsigned long)millis(), vsyncWaitMs,
                                      hsyncWaitMs,
                                      (unsigned long)(millis() - presetsAt),
                                      countWaited,
                                      (unsigned)counted,
                                      (unsigned)GBS::STATUS_SYNC_PROC_HTOTAL::read(),
                                      (unsigned)GBS::HPERIOD_IF::read(),
                                      (unsigned)GBS::STATUS_MISC_PLLAD_LOCK::read(),
                                      (unsigned)GBS::SP_SOG_MODE::read());

                        if (!Tv5725::VideoSignal::countIsSource(counted)) {
                            debugPrintf("DETECT: sync found but no line count after %ums,"
                                        " not taking it\n", countWaited);
                            return 0;
                        }

                        return 3;
                    } else {
                        // 
                    }
                }

                if (Info_sate == 0 &&
                    SyncSearch::searchFor(SeleInputSource, vsyncActive) == SyncSearch::VsyncAbsent) {

                    SYNC_EVENT("det rgb search", 0);
                    Tv5725::SyncMeasurement::set(true);
                    GBS::MD_SEL_VGA60::write(0); 
                    uint16_t testCycle = 0;
                    timeOutStart = millis();
                    while ((millis() - timeOutStart) < 6000) {
                        delay(2);
                        if (Tv5725::VideoSignal::countIsSource(
                                Tv5725::SyncProcessor::lineCount())) {
                            return 1;
                        }
                        testCycle++;
                        
                        if ((testCycle % Tv5725::SyncOnGreen::SearchStepCycles) == 0) {
                            SYNC_EVENT("det rgb sog",
                                       Tv5725::SyncOnGreen::level());
                            Tv5725::SyncOnGreen::choose(Tv5725::SyncOnGreen::nextSearchLevel(
                                Tv5725::SyncOnGreen::level()));
                            Tv5725::SyncOnGreen::putInForce();
                        }

                    }
                    return 1;
                }

                GBS::SP_SOG_MODE::write(1);
                Tv5725::SyncProcessor::reset();
                Tv5725::ModeDetect::reset();
                delay(40);
            } else if (currentInput == 0 && Info_sate == 0) //&& SeleInputSource == S_YUV ) // 20240919
            {
                // **THE SEPARATOR LEVEL IS NOT A LINE-COUNT QUESTION.** This
                // walked the level for 6000 ms waiting for a source count, and
                // the count cannot answer: measured across the whole window at
                // every level, STATUS_SYNC_PROC_VTOTAL, HTOTAL and
                // STATUS_MISC_PLLAD_LOCK all read 0, with PLLAD_MD left on the
                // previous source's value. Nothing can be counted until the
                // engine has sized a divider for the source arriving, which
                // happens after this returns -- so the walk asked an instrument
                // that was dead for the duration and always timed out on
                // ComponentLevel, which is what works.
                //
                // Tv5725::SyncOnGreen::acquire() is the walk that can answer,
                // and it runs on this path: it scores the SEPARATOR BUS rather
                // than the count, so it needs neither a divider nor a locked
                // PLL, and it walks down from ComponentLevel for a source whose
                // sync on green is weaker than this bench's.
                SYNC_EVENT("det ypbpr branch", currentInput);
                GBS::MD_SEL_VGA60::write(0);
                Tv5725::SyncOnGreen::choose(Tv5725::SyncOnGreen::ComponentLevel);
                Tv5725::SyncOnGreen::putInForce();

                return 2;
            }

            ; // SerialMprintln(" lost..");
            Tv5725::SyncOnGreen::choose(2);
            Tv5725::SyncOnGreen::putInForce();
        }

        // A signal arrived and no branch claimed it, so waiting longer
        // answers nothing.
        break;
    }

    if (detectionMayChangeInput()) {
        SYNC_EVENT("det toggle input", !currentInput);
        Tv5725::Adc::selectInput(!currentInput);
        delay(200);
    }

    return 0;
}

uint8_t inputAndSyncDetect() 
{
    // **DETECTION BLOCKS loop(), so what it costs is invisible from the
    // console except as silence.** Its two line-count waits run 6000 ms each
    // and exit early only on a source count, so the duration says which
    // happened and nothing else can.
    // docs/investigations/detection-blocks-the-loop.md
    const unsigned long detectAt = millis();
    uint8_t syncFound = detectAndSwitchToActiveInput();
    if (syncFound != 0) {
        sourceAbsence.found();
    }
    debugPrintf("DETECT: %lums, syncFound %u\n",
                (unsigned long)(millis() - detectAt), (unsigned)syncFound);
    SYNC_EVENT("det found", syncFound);
    // printf(" syncFound = %d \n",syncFound);
    if (syncFound == 0) {
        const bool syncPresent = Tv5725::SyncProcessor::signalPresent();
        SYNC_EVENT("det sync present", syncPresent ? 1 : 0);
        if (syncPresent) {
            sourceAbsence.undecided();
        } else {
            sourceAbsence.missed();
            if (!sourceAbsence.shouldPowerDown()) {
                SYNC_EVENT("det absent", sourceAbsence.passes());
                return 0;
            }
            SYNC_EVENT("det low power", 1);
            rto->sourceDisconnected = true;
            GBS::SP_SOG_MODE::write(1);
            goLowPowerWithInputDetection();
            rto->isInLowPowerMode = true;
            sourceAbsence.poweredDown();
        }
        return 0;
    } else if (syncFound == 1 && Info_sate == 0) //&& SeleInputSource == S_RGBs)
    {
        rto->sourceDisconnected = false;
        rto->isInLowPowerMode = false; 
        applyColourPath();
        if (VideoSourceSelection::selected() == InfoRGBs || VideoSourceSelection::selected() == InfoRGsB) {
        }

        return 1;
    } else if (syncFound == 2 && Info_sate == 0) //&& SeleInputSource == S_YUV)
    {
        rto->isInLowPowerMode = false; 
        rto->sourceDisconnected = false;
        applyColourPath();
        // GBS::VDS_CONVT_BYPS::write(0);
        // GBS::PIP_CONVT_BYPS::write(0);
        if (VideoSourceSelection::selected() == InfoYUV || VideoSourceSelection::selected() == InfoSV || VideoSourceSelection::selected() == InfoAV) {
        }

        return 2;
    } else if (syncFound == 3 && Info_sate == 0) //&& SeleInputSource == S_VGA)
    {
        rto->isInLowPowerMode = false; 
        rto->sourceDisconnected = false;
        applyColourPath();
        Tv5725::RgbhvOutput::chooseBypass();

        return 3;
    }

    return 0;
}


// Read from register
static inline void readFromRegister(uint8_t reg, int bytesToRead, uint8_t *output)
{
    return GBS::read(lastSegment, reg, output, bytesToRead);
}

void printReg(uint8_t seg, uint8_t reg)
{
    uint8_t readout;
    readFromRegister(reg, 1, &readout);

    ; // SerialMprint("0x");
    ; // SerialMprint(readout, HEX);
    ; // SerialMprint(", // s");
    ; // SerialMprint(seg);
    ; // SerialMprint("_");
    ; // SerialMprintln(reg, HEX);
}

void dumpRegisters(byte segment)
{
    if (segment > 5)
        return;
    writeOneByte(0xF0, segment);

    switch (segment) {
        case 0:
            for (int x = 0x40; x <= 0x5F; x++) {
                printReg(0, x);
            }
            for (int x = 0x90; x <= 0x9F; x++) {
                printReg(0, x);
            }
            break;
        case 1:
            for (int x = 0x0; x <= 0x2F; x++) {
                printReg(1, x);
            }
            break;
        case 2:
            for (int x = 0x0; x <= 0x3F; x++) {
                printReg(2, x);
            }
            break;
        case 3:
            for (int x = 0x0; x <= 0x7F; x++) {
                printReg(3, x);
            }
            break;
        case 4:
            for (int x = 0x0; x <= 0x5F; x++) {
                printReg(4, x);
            }
            break;
        case 5:
            for (int x = 0x0; x <= 0x6F; x++) {
                printReg(5, x);
            }
            break;
    }
}

// How long to wait for the source to lock again after moving the divider. The
// sync processor needs a few frames; 1.2 s is several, and short enough that a
// failed attempt does not feel like a hang.
#define PLLAD_LOCK_TIMEOUT_MS 1200

// Move PLLAD_MD and insist the source still locks, or put it back.
//
// PLLAD_MD is the ADC PLL divider, so a value the source cannot be sampled at
// takes sync with it -- and sync is what everything downstream needs to
// recover. FrameSync in particular then waits for vsync edges that never come.
// Leaving the scaler parked on a divider that broke it turns a single bad
// write into a unit that needs a power cycle.
//
// The read-backs are not belt and braces: the I2C bus can stop answering
// mid-sequence around this register, so the value read before the write is
// checked for sanity and the value read after is checked for having taken.
//
// Returns true if the new divider is in place and locked. Otherwise the
// previous value is restored and it returns false, so a caller can report the
// failure instead of continuing against a scaler that has moved out from under
// it.
bool writePllAdMdChecked(uint16_t wanted)
{
    const uint16_t previous = GBS::PLLAD_MD::read();

    // 0 or past the 12-bit field is the bus not answering, not a divider.
    // Writing on top of that would be writing blind, and the restore afterwards
    // would restore nonsense.
    if (previous == 0 || previous > 0x0FFF) {
        debugPrintf("PLLAD_MD: read %u before the write; refusing to touch it\n", previous);
        return false;
    }
    if (wanted == 0 || wanted > 0x0FFF) {
        debugPrintf("PLLAD_MD: %u is not a legal divider\n", wanted);
        return false;
    }
    if (wanted == previous) {
        return true;
    }

    Tv5725::Adc::applyDivider(wanted);

    if (GBS::PLLAD_MD::read() != wanted) {
        debugPrintf("PLLAD_MD: write of %u did not take, restoring %u\n", wanted, previous);
        Tv5725::Adc::applyDivider(previous);
        return false;
    }

    unsigned long deadline = millis() + PLLAD_LOCK_TIMEOUT_MS;
    while ((int32_t)(millis() - deadline) < 0) {
        handleWiFi(0); // the whole point is staying reachable while we wait
        delay(10);
        if (Tv5725::SyncProcessor::hsyncActive()) {
            return true;
        }
    }

    debugPrintf("PLLAD_MD: %u lost sync, restoring %u\n", wanted, previous);
    Tv5725::Adc::applyDivider(previous);
    return false;
}

void resetPLL()
{
    GBS::PLL_VCORST::write(1);
    delay(1);
    GBS::PLL_VCORST::write(0);
    delay(1);
    Tv5725::SyncProcessor::forgetPositions();
}





void moveHS(uint16_t amountToAdd, bool subtracting)
{
    if (Tv5725::VideoRoute::isHdBypassChannel()) {
        uint16_t SP_CS_HS_ST = GBS::SP_CS_HS_ST::read();
        uint16_t SP_CS_HS_SP = GBS::SP_CS_HS_SP::read();
        uint16_t htotal = GBS::HD_HSYNC_RST::read();

        if (sourceLowLineRate()) {
            htotal -= 8;
            htotal *= 2;
        }

        if (htotal == 0)
            return;
        int16_t amount = subtracting ? (0 - amountToAdd) : amountToAdd;

        if (SP_CS_HS_ST + amount < 0) {
            SP_CS_HS_ST = htotal + SP_CS_HS_ST;
        }
        if (SP_CS_HS_SP + amount < 0) {
            SP_CS_HS_SP = htotal + SP_CS_HS_SP;
        }

        GBS::SP_CS_HS_ST::write((SP_CS_HS_ST + amount) % htotal);
        GBS::SP_CS_HS_SP::write((SP_CS_HS_SP + amount) % htotal);

        ; // SerialMprint("HSST: ");
        ; // SerialMprint(GBS::SP_CS_HS_ST::read());
        ; // SerialMprint(" HSSP: ");
        ; // SerialMprintln(GBS::SP_CS_HS_SP::read());
    } else {
        uint16_t VDS_HS_ST = GBS::VDS_HS_ST::read();
        uint16_t VDS_HS_SP = GBS::VDS_HS_SP::read();
        uint16_t htotal = GBS::VDS_HSYNC_RST::read();

        if (htotal == 0)
            return;
        int16_t amount = subtracting ? (0 - amountToAdd) : amountToAdd;

        if (VDS_HS_ST + amount < 0) {
            VDS_HS_ST = htotal + VDS_HS_ST;
        }
        if (VDS_HS_SP + amount < 0) {
            VDS_HS_SP = htotal + VDS_HS_SP;
        }

        GBS::VDS_HS_ST::write((VDS_HS_ST + amount) % htotal);
        GBS::VDS_HS_SP::write((VDS_HS_SP + amount) % htotal);
    }
    printVideoTimings();
}


void invertHS()
{
    uint8_t high, low;
    uint16_t newST, newSP;

    writeOneByte(0xf0, 3);
    readFromRegister(0x0a, 1, &low);
    readFromRegister(0x0b, 1, &high);
    newST = ((((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
    readFromRegister(0x0b, 1, &low);
    readFromRegister(0x0c, 1, &high);
    newSP = ((((uint16_t)high) & 0x00ff) << 4) | ((((uint16_t)low) & 0x00f0) >> 4);

    uint16_t temp = newST;
    newST = newSP;
    newSP = temp;

    writeOneByte(0x0a, (uint8_t)(newST & 0x00ff));
    writeOneByte(0x0b, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)));
    writeOneByte(0x0c, (uint8_t)((newSP & 0x0ff0) >> 4));
}

void invertVS()
{
    uint8_t high, low;
    uint16_t newST, newSP;

    writeOneByte(0xf0, 3);
    readFromRegister(0x0d, 1, &low);
    readFromRegister(0x0e, 1, &high);
    newST = ((((uint16_t)high) & 0x000f) << 8) | (uint16_t)low;
    readFromRegister(0x0e, 1, &low);
    readFromRegister(0x0f, 1, &high);
    newSP = ((((uint16_t)high) & 0x00ff) << 4) | ((((uint16_t)low) & 0x00f0) >> 4);

    uint16_t temp = newST;
    newST = newSP;
    newSP = temp;

    writeOneByte(0x0d, (uint8_t)(newST & 0x00ff));
    writeOneByte(0x0e, ((uint8_t)(newSP & 0x000f) << 4) | ((uint8_t)((newST & 0x0f00) >> 8)));
    writeOneByte(0x0f, (uint8_t)((newSP & 0x0ff0) >> 4));
}

uint16_t getCsVsStart()
{
    return (GBS::SP_SDCS_VSST_REG_H::read() << 8) + GBS::SP_SDCS_VSST_REG_L::read();
}

uint16_t getCsVsStop()
{
    return (GBS::SP_SDCS_VSSP_REG_H::read() << 8) + GBS::SP_SDCS_VSSP_REG_L::read();
}

// Dump the scaler's live display timings: on demand from the web UI (`/sc?,`)
// or serial (`,`), and after every moveHS() nudge. printf_P gives one
// WebSocket frame per line, with the format strings left in flash.
void printVideoTimings()
{
#if GBS_DEBUG
    // NULL is nothing chosen yet, which is not bypass.
    const Tv5725::OutputMode *const out = geometry.outputMode();
    if (out == NULL || !out->isBypass()) {
        SerialM.printf_P(PSTR("\nHT / scale   : %d %d\n"), GBS::VDS_HSYNC_RST::read(), GBS::VDS_HSCALE::read());
        SerialM.printf_P(PSTR("HS ST/SP     : %d %d\n"), GBS::VDS_HS_ST::read(), GBS::VDS_HS_SP::read());
        SerialM.printf_P(PSTR("HB ST/SP(d)  : %d %d\n"), GBS::VDS_DIS_HB_ST::read(), GBS::VDS_DIS_HB_SP::read());
        SerialM.printf_P(PSTR("HB ST/SP     : %d %d\n"), GBS::VDS_HB_ST::read(), GBS::VDS_HB_SP::read());
        SerialM.printf_P(PSTR("------\n"));
        // vertical
        SerialM.printf_P(PSTR("VT / scale   : %d %d\n"), GBS::VDS_VSYNC_RST::read(), GBS::VDS_VSCALE::read());
        SerialM.printf_P(PSTR("VS ST/SP     : %d %d\n"), GBS::VDS_VS_ST::read(), GBS::VDS_VS_SP::read());
        SerialM.printf_P(PSTR("VB ST/SP(d)  : %d %d\n"), GBS::VDS_DIS_VB_ST::read(), GBS::VDS_DIS_VB_SP::read());
        SerialM.printf_P(PSTR("VB ST/SP     : %d %d\n"), GBS::VDS_VB_ST::read(), GBS::VDS_VB_SP::read());
        // IF V offset
        SerialM.printf_P(PSTR("IF VB ST/SP  : %d %d\n"), GBS::IF_VB_ST::read(), GBS::IF_VB_SP::read());
    } else {
        SerialM.printf_P(PSTR("\nHD_HSYNC_RST : %d\n"), GBS::HD_HSYNC_RST::read());
        SerialM.printf_P(PSTR("HD_INI_ST    : %d\n"), GBS::HD_INI_ST::read());
        SerialM.printf_P(PSTR("HS ST/SP     : %d %d\n"), GBS::SP_CS_HS_ST::read(), GBS::SP_CS_HS_SP::read());
        SerialM.printf_P(PSTR("HB ST/SP     : %d %d\n"), GBS::HD_HB_ST::read(), GBS::HD_HB_SP::read());
        SerialM.printf_P(PSTR("------\n"));
        // vertical
        SerialM.printf_P(PSTR("VS ST/SP     : %d %d\n"), GBS::HD_VS_ST::read(), GBS::HD_VS_SP::read());
        SerialM.printf_P(PSTR("VB ST/SP     : %d %d\n"), GBS::HD_VB_ST::read(), GBS::HD_VB_SP::read());
    }

    SerialM.printf_P(PSTR("CsVT         : %d\n"), GBS::STATUS_SYNC_PROC_VTOTAL::read());
    SerialM.printf_P(PSTR("CsVS_ST/SP   : %d %d\n"), getCsVsStart(), getCsVsStop());
#endif
}


// One line per IR frame, naming which of loop()'s consumers took it.
//
// decode() does not consume a frame -- resume() does -- so a frame the
// described menu declines is counted again by whichever consumer answers it.
// The counts say where a press WENT, not how many arrived. A press that reached
// the engine also prints an ADJ line, so "decoded but did nothing" is a trace
// line with no ADJ after it, and `worst loop` is what decides whether a press
// survived at all: a frame arriving while the receiver has not been resumed is
// dropped. docs/osd-menu.md
static uint32_t irWorstLoopMs = 0;

static void traceIrFrames(uint32_t byDescribedMenu, uint32_t byOsdIr,
                          int menuBefore)
{
    if (byDescribedMenu == 0 && byOsdIr == 0)
        return;
    debugPrintf("IR value:0x%08lX  described:%lu OSD_IR:%lu  menu:%d->%d  "
                "worst loop since last frame:%lums\n",
                (unsigned long)results.value, (unsigned long)byDescribedMenu,
                (unsigned long)byOsdIr, menuBefore, oled_menuItem,
                (unsigned long)irWorstLoopMs);
    irWorstLoopMs = 0;
}

// The sink src/tv5725/ composes its diagnostics for. Declared in
// Tv5725Log.h and defined here, so a class under src/ can report without
// reaching for SerialM -- which lives above it and does not host-compile.
void tv5725Log(const char *message)
{
    debugPrintf("%s\n", message);
}




// The ESP's half of src/tv5725/DebugPin.h: time one period of whatever the chip
// has selected onto the debug pin, and say what one tick is worth. Everything
// here is an ESP API the engine layer can neither reach nor host-compile.

// How long one measurement waits for its two edges. It needs one to arm and a
// second to measure, so the worst case is two frame periods -- 40 ms at 50 Hz
// -- plus the delay(7). 250 ms is comfortably above that and far short of the
// interval that drops WiFi.
#define FS_SAMPLE_TIMEOUT_MS 250

// Spins between deadline checks. The wait has to stay a tight poll on a
// volatile: millis() and ESP.wdtFeed() are function calls, and doing both on
// every pass slows it enough to stop a measurement completing.
#define FS_SAMPLE_CHECK_EVERY 1024

// How long debugPinProbe() watches each selector. 25 ms is over one frame at
// 50 Hz, so a working vsync must show transitions.
#define FS_PROBE_MS 25

// Rate limit, so a unit that fails twice a second does not fill the console.
#define FS_PROBE_INTERVAL_MS 2000

namespace MeasurePeriod
{
    volatile uint32_t stopTime, startTime;
    volatile uint32_t armed;

    void _risingEdgeISR_prepare();
    void _risingEdgeISR_measure();

    void start()
    {
        startTime = 0;
        stopTime = 0;
        armed = 0;
        attachInterrupt(DEBUG_IN_PIN, _risingEdgeISR_prepare, RISING);
    }

    // A completed measurement detaches itself -- _measure() is the last ISR and
    // it detaches on the way out. A measurement that times out does not, so the
    // caller has to, or an edge arriving afterwards writes startTime behind the
    // back of whoever reads it next.
    void stop()
    {
        detachInterrupt(DEBUG_IN_PIN);
    }

    void ICACHE_RAM_ATTR _risingEdgeISR_prepare()
    {
        noInterrupts();
        __asm__ __volatile__("rsr %0,ccount"
                             : "=a"(startTime));
        detachInterrupt(DEBUG_IN_PIN);
        armed = 1;
        attachInterrupt(DEBUG_IN_PIN, _risingEdgeISR_measure, RISING);
        interrupts();
    }

    void ICACHE_RAM_ATTR _risingEdgeISR_measure()
    {
        noInterrupts();
        __asm__ __volatile__("rsr %0,ccount"
                             : "=a"(stopTime));
        detachInterrupt(DEBUG_IN_PIN);
        interrupts();
    }
}

// **THE WAIT IS BOUNDED IN TIME, AND THE WATCHDOG STAYS RUNNING.** Bounding it
// by loop passes instead, with the watchdog off, holds the CPU long enough that
// serial, ping and HTTP all die while the picture keeps running -- the TV5725 is
// a separate chip -- and the caller re-enters immediately, so a bounded stall
// behaves like a permanent wedge. A PLLAD_MD write big enough to break sync is
// exactly how you get here.
//
// Deliberately no yield() in the spin. The timestamps come from the two
// ICACHE_RAM_ATTR edge ISRs reading ccount, so this loop is a pure wait -- but
// yield() runs the WiFi stack, whose interrupts-off sections would delay an edge
// ISR and skew the timestamp it records. The period resolves to about one cycle
// in three million, and a few thousand cycles of added interrupt latency would
// swamp that. The delay(7) after the first edge stays exactly where it is: it
// yields in the ~20 ms of slack between edges, well away from the one that is
// about to be measured.
bool debugPinPulseEdges(uint32_t *start, uint32_t *stop)
{
    yield();
    ESP.wdtFeed();
    MeasurePeriod::start();

    const uint32_t deadline = millis() + FS_SAMPLE_TIMEOUT_MS;
    uint32_t spins = 0;
    while (MeasurePeriod::stopTime == 0)
    {
        if (MeasurePeriod::armed)
        {
            MeasurePeriod::armed = 0;
            delay(7);
            WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
        }
        if (++spins % FS_SAMPLE_CHECK_EVERY == 0)
        {
            // Signed difference, so this still terminates across the millis()
            // wrap rather than spinning for another 49 days.
            if ((int32_t)(millis() - deadline) >= 0)
            {
                break;
            }
            ESP.wdtFeed();
        }
    }

    *start = MeasurePeriod::startTime;
    *stop = MeasurePeriod::stopTime;
    MeasurePeriod::stop();
    WiFi.setSleepMode(WIFI_NONE_SLEEP);

    // Cycle counter overflow, or no pulse at all.
    return *start != 0 && *stop != 0 && *start < *stop;
}

uint32_t debugPinPulseTicks()
{
    uint32_t start, stop;
    return debugPinPulseEdges(&start, &stop) ? stop - start : 0;
}

uint32_t debugPinTicksPerSecond() { return ESP.getCpuFreqMHz() * 1000000; }

#if GBS_DEBUG
void debugPinProbe()
{
    static uint32_t lastProbe = 0;
    const uint32_t now = millis();
    if (lastProbe != 0 && (int32_t)(now - (lastProbe + FS_PROBE_INTERVAL_MS)) < 0)
    {
        return;
    }
    lastProbe = now;

    // Sweep the selectors this firmware uses elsewhere, so a pin that is simply
    // on the wrong bus can be told from one that is dead. 0x0 is what framesync
    // measures on, 0x2 is VDS, 0xa is what the sync watcher and the HTotal
    // search use. If none of them move it, the fault is the pin or the net.
    const uint8_t selectors[] = {0x0, 0x2, 0xa};
    const Tv5725::TestBus::Hold held;

    for (uint8_t i = 0; i < sizeof(selectors); i++)
    {
        Tv5725::TestBus::select(selectors[i]);
        delay(1); // let the mux settle before counting

        int level = digitalRead(DEBUG_IN_PIN);
        const int first = level;
        uint32_t transitions = 0;
        uint32_t spins = 0;

        const uint32_t deadline = millis() + FS_PROBE_MS;
        while ((int32_t)(millis() - deadline) < 0)
        {
            const int sample = digitalRead(DEBUG_IN_PIN);
            if (sample != level)
            {
                transitions++;
                level = sample;
            }
            if (++spins % FS_SAMPLE_CHECK_EVERY == 0)
            {
                ESP.wdtFeed();
            }
        }

        debugPrintf(
            "  DEBUG_IN_PIN sel=0x%x: %u transitions in %ums, level %d->%d, %u samples\n",
            selectors[i], transitions, (unsigned)FS_PROBE_MS, first, level, spins);
    }
}
#else
void debugPinProbe() {}
#endif

// The user picked a different output resolution. Not a source event: the rate
// and the divider the last solve measured still describe the source, so the
// engine re-solves raster, clock and windows from what it holds and nothing is
// re-detected, reset or measured.
//
// Falls back to a whole load only where the engine cannot re-solve -- nothing
// solved yet, bypass, or a mode change already in flight. The blank is the
// caller's, taken before this runs.
static void changeOutputResolution()
{
    const Tv5725::OutputMode *const chosen = chosenOutputMode();

    if (!inputAcquisition.setOutputResolution(chosen)) {
        applyPresets();
        return;
    }

    geometry.applyOutputPictureFilters(uopt->wantSharpness, uopt->wantStepResponse);

    // The raster moved, so the ratio the frequency lock steers by is stale.
    frameSync.cleanup();
    frameSync.clearFrequency();
    frameSync.matchRate(sourceSampling.settledFieldRateHz());
}

void doPostPresetLoadSteps()
{
    // Only where something held the blocks and so discarded their
    // configuration -- low power and the RGBHV watchdog through
    // setResetParameters(), and setOutModeHdBypass(). Boot brings the chip up,
    // so a mode change does not repeat it.
    if (Tv5725::BringUp::armed())
        Tv5725::BringUp::init(inputFormatter);

    // Beside ModeDetect::init() inside that block and travelling with it: both
    // depend on runtime state rather than on any table.
    Tv5725::ModeDetect::applySyncType(Tv5725::SyncMeasurement::isCsync()
                                          ? Tv5725::ModeDetect::Csync
                                          : Tv5725::ModeDetect::SeparateSync);

    // if(Info_sate == 0)
    {
        Tv5725::Chip::enableClockInputPad();

        // BEFORE prepareSyncProcessor(), which is the per-load setup that does
        // not follow the sync type. Asked on every route rather than only the
        // scaling-RGBHV one: a source that never reaches it keeps whatever sync
        // path the last one left, and a separate-sync source left on
        // sync-on-green counts nothing at all.
        Tv5725::SyncProcessor::applyForSyncType(Tv5725::SyncMeasurement::isCsync(),
                                                sourceHasSerratedSync());
        prepareSyncProcessor();
        if (scalingRgbhv()) {
            if (Tv5725::SyncMeasurement::isCsync()) {
                Tv5725::SyncOnGreen::choose(24);
            }
            Tv5725::Adc::choosePhaseAdc(16);
            Tv5725::Adc::choosePhaseSyncProcessor(8);
        }

        Tv5725::SyncProcessor::setHsyncOverflowProtect(false);
        Tv5725::SyncProcessor::setCoastInvert(false);
        if (!Tv5725::VideoRoute::isHdBypassChannel() && !Tv5725::PresetLoad::scalingRgbhvInForce()) {
            inputAcquisition.applySyncProcessorDynamic(0);
        }

        Tv5725::SyncProcessor::holdClamp();

        applyColourPath();

        if (Tv5725::VideoRoute::isHdBypassChannel()) {
            Tv5725::Chip::OUT_SYNC_SEL::write(1);
        }

        Tv5725::Adc::choosePhaseSyncProcessor(8);

        // The level is NOT chosen here. VideoSourceAcquisition::acquireSeparatorLevel()
        // owns it, seeds from Adc::inputIsComponent() and then searches -- so a
        // value stated here is a second owner writing a final answer where the
        // engine writes a starting point. Whatever is held goes in force
        // because the separator read below is taken through it.
        Tv5725::SyncOnGreen::apply();
        Tv5725::Adc::applyPhases();

        geometry.forgetPreviousSource();
        rto->sourceDisconnected = false;
        Tv5725::Chip::holdPower(true);



        if (Tv5725::SyncMeasurement::isCsync()) {
            if (Tv5725::TestBus::readHigh() == 0) {
                delay(4);
                if (Tv5725::TestBus::readHigh() == 0) {
                    inputAcquisition.acquireSeparatorLevel();
                    delay(4);
                }
            }
        }

        // **THE SAMPLING DIVIDER.** PLLAD_MD, IF_HSYNC_RST and SP_RT_HS_SP are
        // ONE quantity in three registers, and Tv5725::SourceMeasurement is the
        // single owner of all three. Tv5725::Adc writes the divider and latches
        // it, so the write-before-latch ordering is no longer this caller's.
        //
        // The source is about to change mode, and nothing measurable about it
        // is true yet. Everything the solve needs that cannot be re-derived
        // later goes with the message; loop() drives the rest once the source
        // has settled into the new mode.
        //
        // The most the clock can carry, for every source: the decimators undo
        // the faster tap so the same samples a line reach the pipeline either
        // way, and they filter. applySampleRate() clamps it to the crossover
        // row. docs/investigations/the-decimators-filter.md
        geometry.inputTimingsChanged(Tv5725::Adc::OversampleAsClockAllows);

        Tv5725::Adc::armGainMeasurement(uopt->enableAutoGain == 1);

        Tv5725::Adc::applyHeldOffset();

        geometry.applyPictureFilters(uopt->wantVdsLineFilter, uopt->wantPeaking);
        geometry.applyOutputPictureFilters(uopt->wantSharpness, uopt->wantStepResponse);

        frameSync.cleanup();
        frameTimeLock.forgiveFailures();

        inputAcquisition.placeCoastWindow(0);
        inputAcquisition.placeClampWindow();


        Tv5725::Chip::resetVideoBlocks();

        Tv5725::Adc::restartPll();
        Tv5725::SyncProcessor::forgetPositions();
        geometry.applyClockGroup();

        // **DO NOT DISABLE CAP_SAFE_GUARD_EN HERE.** Tv5725::FrameBuffer
        // owns that bit and switches it ON; a write here runs later in this
        // same function and wins, leaving the capture buffer unbounded.
        //
        // Upstream disabled it against a memory map whose capture buffer
        // started at 0x100000, only 356 KB below the guard address, where a
        // large capture could genuinely trip it. MemoryWindow puts the guard at
        // the top of the address space with the engine clamping the capture
        // below it, so nothing but a real overrun reaches it.

        geometry.applyFrameBufferRequests();
        // PB_CAP_OFFSET = PB_FETCH_NUM + 4 was here for standards 3 and 4.
        // Both halves of that pair are Tv5725::Memory's: the offset is
        // MemoryWindow::strideFor(the output line) and the fetch is computed
        // against it, so deriving one from the other after the fact could
        // only fight the model. VideoPath::write() sets both.

        // applyClockGroup() wrote the group PLLAD_LAT loads on a rising edge,
        // and the decimator modes beside it -- so the adjusters take their
        // value again on the restart applyPhases() ends with.
        Tv5725::Adc::applyPhases();
        Tv5725::Adc::latch();

        Tv5725::SyncProcessor::clampFromReferenceClock();
        Tv5725::SyncProcessor::applyDefaultClampWindow();

        Tv5725::SyncProcessor::setHsyncOverflowProtect(false);

        if (Tv5725::SyncMeasurement::isCsync()) {
            Tv5725::SyncProcessor::selectExternalSync(1);
        }

        Tv5725::SyncProcessor::forgetPositions();

        if (Tv5725::VideoRoute::isHdBypassChannel()) {
                    Tv5725::Interrupts::acknowledgeAll();

            // Video routes around the VDS here, so the mode change armed above
            // has no solve coming and the freeze it took would never be
            // released.
            geometry.setOutputMode(&Tv5725::ModeBypass);

            return;
        }

        inputAcquisition.placeClampWindow();
        if (Tv5725::SyncProcessor::clampPlaced()) {
            if (Tv5725::SyncProcessor::clampHeld()) {
                Tv5725::SyncProcessor::releaseClamp();
            }
        }

        inputAcquisition.applySyncProcessorDynamic(0);

        if (!rto->syncWatcherEnabled) {
            Tv5725::SyncProcessor::releaseClamp();
        }

        Tv5725::Interrupts::acknowledgeAll();

        // Pass-through is not decided here. It is a statement about the
        // measured source, and this runs at the end of a preset load with
        // whatever the PREVIOUS source measured still held --
        // VideoSourceAcquisition::passSourceThrough() owns it and re-answers it
        // from each measurement. docs/video-source-acquisition.md
        rto->applyPresetDoneStage = 1;

        // Capture stays frozen: it is released by the poll() that lands the
        // windows, seconds from now once the source has settled into the new
        // mode. Releasing it here shows the previous mode's geometry against
        // the new source until then.

    }
}

void applyPresets()
{
    // Frozen: docs/gbs-control-debug-interface.md
    if (AUTOMATION_FROZEN()) {
        return;
    }

    // printf("result %d \n", result);
    if (!Tv5725::Chip::hasPower()) {
        return;
    }

    // WHICH CONNECTOR THE SOURCE ARRIVES ON, not what the classification calls
    // it. The byte reached this test as Rgbhv and skipped it as BypassRgbhv,
    // which is the same source with a different output chosen for it, so a
    // source passed through never had its sync type established here at all.
    if (sourceIsRgbhv()) {
        if (Tv5725::SyncProcessor::hsyncActive()) {

            // **DO NOT DECIDE THE SYNC TYPE FROM STATUS_SYNC_PROC_VSACT.** That
            // is circular -- VSACT only reports correctly once the sync type is
            // already right, so the choice latches to whatever the chip happens
            // to be configured for. Landing in the wrong basin has
            // applySyncProcessorDynamic() write the separate-sync quadruple (SP_PRE_COAST
            // 0, SP_POST_COAST 0, SP_DLT_REG 0, SP_H_PULSE_IGNOR 0xFF) onto a
            // source with no separate sync. docs/sync-type-selection.md
            //
            // sourceHasOwnVsync() answers it properly: it switches
            // SP_EXT_SYNC_SEL off and asks whether a V sync line actually
            // arrives. It costs ~500 ms, so it runs ONCE PER SOURCE, and a mode
            // change pays nothing.
            //
            // **WHAT RE-ARMS IT IS A CHANGE OF SOURCE, NOT A CLEARED CLAMP.**
            // SyncMeasurement::forget() sits beside SyncProcessor::forgetPositions()
            // at the five sites that mean a different source
            // may now be attached -- the resets, the low-power entry, and
            // LoadDefault() on the input handlers. Six OTHER sites clear those
            // two flags and deliberately do NOT forget, because they are mode
            // changes on the source already attached: twice in this function
            // and in the serial clock-generator command. Reading the flags as the rule re-probes
            // on every preset load and costs 500 ms a time.
            //
            // **IT CANNOT BE LEFT TO inputAndSyncDetect() ALONE.** Its probe
            // sits behind `SyncSearch::searchFor(...) == VsyncPresent`, so on a
            // source with no separate vsync -- the csync case -- the block is
            // skipped and the sync type keeps its initial false. Circular in the
            // same way, one level up.
            {
                // Whether it probed, not just what it holds: the message said
                // "probed once" either way, so a probe suppressed by a stale
                // answer read exactly like one that ran.
                const bool measured = !Tv5725::SyncMeasurement::isSet();
                Tv5725::SyncMeasurement::syncType(syncTypeHasOwnVsync);
                debugPrintf("sync type: %s for this source -> %s\n",
                    measured ? "probed" : "already held",
                    Tv5725::SyncMeasurement::isCsync() ? "csync" : "separate H/V");
            }
        }
    }

    // Coming off the channel, or from no source at all, so these three blocks
    // are held and the load has to release them. Every source, because what the
    // byte excluded -- 5, 6, 7, 13 and 15 -- were the values that used to be
    // routed to the channel from here, and pass-through is not chosen here any
    // more. A block left held shows nothing whatever the preset writes.
    if (Tv5725::VideoRoute::isHdBypassChannel() || rgbhvBypass()
        || !inputAcquisition.sourceIsPresent()) {
        GBS::SFTRST_IF_RSTZ::write(1);
        GBS::SFTRST_VDS_RSTZ::write(1);
        GBS::SFTRST_DEC_RSTZ::write(1);
    }
    Tv5725::VideoRoute::toScaler(); // 


    if (GBS::ADC_UNUSED_62::read() != 0x00) {

        serialCommand = 'D';
    }

    // No horizontal sync is the whole of what the byte's 0 meant here, and the
    // sync processor answers it directly.
    if (!Tv5725::SyncProcessor::hsyncActive()) {

        if (detectionMayChangeInput())
            Tv5725::Adc::selectInput(1);
        delay(100);
        if (GBS::STATUS_SYNC_PROC_HSACT::read() == 1) {
            rto->syncWatcherEnabled = 1;

            // HERE the probe IS worth its ~500 ms, and the bare VSACT read is
            // not: this arm has just moved ADC_INPUT_SEL, so whatever detection
            // concluded was about a different input and there is nothing to
            // inherit. It runs only where no horizontal sync was found at all,
            // not on a mode change.
            Tv5725::SyncMeasurement::probe(syncTypeHasOwnVsync);
        } else {
            if (detectionMayChangeInput())
                Tv5725::Adc::selectInput(0);
            delay(100);
            if (GBS::STATUS_SYNC_PROC_HSACT::read() == 1) {
                Tv5725::SyncMeasurement::set(1);
                rto->syncWatcherEnabled = 1;
            } else // 
            {
                // setResetParameters();
                // printf("End \n");
                return;
            }
        }
    }

    // PalForce60's 2 -> 1 and 4 -> 3 swap was here: the option remains, and
    // toggling it now changes nothing. It mapped a PAL standard onto its NTSC
    // twin so a table keyed on the byte would load the 60 Hz raster, and there
    // are no tables. Removing the option itself means removing its OLED and TV
    // OSD menu items, which is a menu-layout change only a remote can check.
    // docs/video-source-acquisition.md

    // **TWO BRANCHES AND TWELVE TABLE LOADS WERE HERE, AND THEY DIFFERED IN
    // NOTHING BUT WHICH TABLE.** One branch per source standard, each a ladder
    // on presetPreference picking a pal_* or ntsc_* blob. The preference is the
    // resolution now, every register is computed from it, and a dispatch on
    // eleven of the byte's fifteen values stood in front of one call.
    //
    // Pass-through is not a preset either. A source asking for it loads this
    // same path, which shows any rate, and whether it is actually passed
    // through is answered from the measurement that follows, by
    // VideoSourceAcquisition::passSourceThrough() -- the only caller with one.
    // docs/video-source-acquisition.md
    loadComputedPreset(chosenOutputMode());

    // The output an RGBHV source is entitled to. Held beside the source rather
    // than in the byte, which carried both facts in one number.
    if (sourceIsRgbhv()) {
        Tv5725::RgbhvOutput::chooseScaling();
        rto->isValidForScalingRGBHV = true;
    }

    doPostPresetLoadSteps();
}


void advancePhase()
{
    Tv5725::Adc::nudgePhaseAdc();
    setAndLatchPhaseADC();
}


void setAndLatchPhaseSP()
{
    Tv5725::Adc::applyPhaseSyncProcessor(Tv5725::Adc::phaseSyncProcessor());
}

void setAndLatchPhaseADC()
{
    Tv5725::Adc::applyPhaseAdc(Tv5725::Adc::phaseAdc());
}


// Restart the blocks a bypass switch has just reconfigured, then load what it
// chose.
//
// **THE LATCHES ARE LAST, AND THAT IS THE ORDERING CONSTRAINT.** Adc::latch() is
// what loads PLLAD_MD, ND, KS, CKOS and ICP into the ADC PLL, on a rising edge.
// Everything choosing those has to run BEFORE this: written after, the registers
// read the new divider while the PLL still clocks the old one, which is a solid
// green screen with nothing self-inconsistent to diagnose from.
// Tv5725::SourceMeasurement, and docs/tv5725-chip.md.
//
// The delays are settling times for the blocks either side of them, measured
// rather than derived.
static void restartAfterBypassSwitch()
{
    Tv5725::Chip::resetVideoBlocks();
    Tv5725::SyncProcessor::reset();
    delay(2);
    Tv5725::MemoryBus::restart();
    delay(2);
    Tv5725::Adc::restartPll();
    Tv5725::SyncProcessor::forgetPositions();
    delay(20);
    Tv5725::Chip::outputUp();

    setAndLatchPhaseSP();
    setAndLatchPhaseADC();
    Tv5725::Adc::latch();
}

// The one entry to pass-through, for every source that reaches it.
//
// It is NOT a preset load. Nothing here re-runs the scaling bring-up, and
// Tv5725::BringUp::arm() is the whole of what this owes the scaling path: the
// fields written below have no owner there, so the next scaled load claims
// them back by bringing the chip up again.
//
// docs/investigations/one-bypass-route-carries-rgbhv.md
// The engine decides pass-through and this is the user's veto, so it has to
// reach the engine every time it changes rather than being read from uopt where
// the decision is taken. docs/video-source-acquisition.md
static void applyPassThroughPreference()
{
    inputAcquisition.allowPassThrough(!uopt->preferScalingRgbhv);
}

void enterHdBypass()
{
    if (!Tv5725::Chip::hasPower()) {
        return;
    }

    SYNC_EVENT("bypass-switch", GBS::STATUS_SYNC_PROC_VTOTAL::read());

    Tv5725::BringUp::arm();

    // Down across the whole switch. Dropping HSOUT/VSOUT is what makes the
    // encoder re-acquire the timing underneath it; restartAfterBypassSwitch()
    // raises them again. docs/investigations/encoder-stale-timing.md
    Tv5725::Chip::outputDown();

    // Video routes around the VDS here, so no solve is coming.
    geometry.setOutputMode(&Tv5725::ModeBypass);

    externalClockGenResetClock();
    frameSync.cleanup();
    GBS::ADC_UNUSED_62::write(0x00);
    GBS::PA_ADC_BYPSZ::write(1);
    GBS::PA_SP_BYPSZ::write(1);
    Tv5725::SyncProcessor::forgetPositions();

    // The ADC's sense of what arrives on R, G and B.
    applyColourPath();

    Tv5725::HdBypass::enterFor(Tv5725::Adc::inputIsComponent(),
                               Tv5725::SyncMeasurement::isCsync(),
                               sourceSampling.lineRateHz(),
                               geometry.sourceTiming(),
                               sourceSampling.sourceLines() + 1,
                               sourceSampling.hsync());

    restartAfterBypassSwitch();

    Tv5725::Adc::armGainMeasurement(uopt->enableAutoGain == 1);
    Tv5725::SyncOnGreen::putInForce();

    // The branch that sends a source here clears
    // rto->isValidForScalingRGBHV in RAM only, and outside the low-power path
    // nothing else clears the register -- so without this the bit says the
    // opposite of the truth, and several sites read it back to decide things.
    Tv5725::PresetLoad::forgetScalingRgbhv();

    delay(200);

    // The only phase search on this route: nothing maintains a source while it
    // is passed through, because there is no scaled path to maintain.
    inputAcquisition.acquireSamplingPhase();
}

void runAutoGain() //
{
    // Frozen: docs/gbs-control-debug-interface.md
    if (AUTOMATION_FROZEN()) {
        return;
    }
    static unsigned long lastTimeAutoGain = millis();
    uint8_t limit_found = 0, greenValue = 0;
    uint8_t loopCeiling = 0;
    uint8_t status00reg = GBS::STATUS_00::read();

    if ((millis() - lastTimeAutoGain) < 30000) {
        loopCeiling = 61;
    } else {
        loopCeiling = 8;
    }

    for (uint8_t i = 0; i < loopCeiling; i++) {
        if (i % 20 == 0) {
            handleWiFi(0);
            limit_found = 0;
        }
        greenValue = Tv5725::TestBus::readHigh();

        if (greenValue == 0x7f) {
            if (Tv5725::SyncProcessor::hsyncActive() && (GBS::STATUS_00::read() == status00reg)) {
                limit_found++;
            } else
                return;

            if (limit_found == 2) {
                limit_found = 0;
                uint8_t level = GBS::ADC_GGCTRL::read();
                if (level < 0xfe) {
                    Tv5725::Adc::holdGain(level + 2, level + 2, level + 2);

                    printInfo();
                    delay(2);
                    lastTimeAutoGain = millis();
                }
            }
        }
    }
}

void enableScanlines()
{
    Tv5725::Deinterlacer::enableScanlines(uopt->scanlineStrength);
}

void disableScanlines()
{
    Tv5725::Deinterlacer::disableScanlines();
}

void enableMotionAdaptDeinterlace() //
{
    const uint8_t verticalTap =
        Tv5725::Deinterlacer::verticalTapFor(
            inputFormatter.verticalPeriod());

    Tv5725::Deinterlacer::enableMotionAdapt(verticalTap,
                                            Tv5725::FrameBuffer::releaseCapture);
}

void disableMotionAdaptDeinterlace() // 
{
    Tv5725::Deinterlacer::disableMotionAdapt();
}

void printInfo()
{
    static char print[121];
    static uint8_t clearIrqCounter = 0;
    static uint8_t lockCounterPrevious = 0;
    uint8_t lockCounter = 0;

    int32_t wifi = 0;
    if ((WiFi.status() == WL_CONNECTED) || (WiFi.getMode() == WIFI_AP)) {
        wifi = WiFi.RSSI();
    }

    uint16_t hperiod = GBS::HPERIOD_IF::read();

    // `v:` and `vt:` below are different blocks measuring the same thing: the
    // input formatter and the sync processor. On RGBHV the IF never completes a
    // vertical measurement, so VPERIOD_IF is debris while
    // STATUS_SYNC_PROC_VTOTAL is correct. Printed as two plain numbers they look
    // equally authoritative, so STATUS_IF_VT_BAD decides which is shown.
    char vperiodText[8];
    if (GBS::STATUS_IF_VT_BAD::read()) {
        snprintf(vperiodText, sizeof(vperiodText), "%4s", "----");
    } else {
        snprintf(vperiodText, sizeof(vperiodText), "%4u", GBS::VPERIOD_IF::read());
    }
    uint8_t stat0FIrq = GBS::STATUS_0F::read();
    char HSp = GBS::STATUS_SYNC_PROC_HSPOL::read() ? '+' : '-';
    char VSp = GBS::STATUS_SYNC_PROC_VSPOL::read() ? '+' : '-';
    char h = 'H', v = 'V';
    if (!GBS::STATUS_SYNC_PROC_HSACT::read()) {
        h = HSp = ' ';
    }
    if (!GBS::STATUS_SYNC_PROC_VSACT::read()) {
        v = VSp = ' ';
    }

    // printf(print, ...) passed `print` — the output buffer — as the format
    // string, a mangled sprintf. Since `print` is static and nothing ever writes
    // to it, this formatted an empty string: printInfo() has printed nothing at
    // all, whatever the docs said. It is the only view of the acquisition run --
    // `u:` unmeasured passes, `s:` acquired -- which lives in ESP RAM and so
    // cannot be read back over I2C, so it is worth having.
    //
    // Rate limited, and only the output is: loop() calls printInfo() every
    // iteration with no gate of its own, and SerialM broadcasts to the
    // WebSocket, so unthrottled this floods the socket until the heap check in
    // SerialMirror disconnects it. The interrupt handling below still runs at
    // the original rate.
    static unsigned long lastInfoPrint = 0;
    if ((millis() - lastInfoPrint) >= 250) {
        lastInfoPrint = millis();
        snprintf(print, sizeof(print),
            "h:%4u v:%4s PLL:%01u A:%02x%02x%02x S:%02x.%02x.%02x %c%c%c%c I:%02x D:%04x ht:%4d vt:%4d hpw:%4d u:%3x s:%2x S:%2d W:%2d\n",
            hperiod, vperiodText, lockCounterPrevious,
            GBS::ADC_RGCTRL::read(), GBS::ADC_GGCTRL::read(), GBS::ADC_BGCTRL::read(),
            GBS::STATUS_00::read(), GBS::STATUS_05::read(), GBS::SP_CS_0x3E::read(),
            h, HSp, v, VSp, stat0FIrq, Tv5725::TestBus::read(),
            GBS::STATUS_SYNC_PROC_HTOTAL::read(), GBS::STATUS_SYNC_PROC_VTOTAL::read() /*+ 1*/,
            GBS::STATUS_SYNC_PROC_HLOW_LEN::read(), inputAcquisition.unmeasuredPasses(), inputAcquisition.acquiredPasses(),
            Tv5725::SyncOnGreen::level(), wifi);
        SerialM.print(print);
    }
    if (stat0FIrq != 0x00) {
        clearIrqCounter++;
        if (clearIrqCounter >= 50) {
            clearIrqCounter = 0;
            Tv5725::Interrupts::acknowledgeAll();
        }
    }

    yield();
    if (GBS::STATUS_SYNC_PROC_HSACT::read()) {
        for (uint8_t i = 0; i < 9; i++) {
            if (GBS::STATUS_MISC_PLLAD_LOCK::read() == 1) {
                lockCounter++;
            } else {
                for (int i = 0; i < 10; i++) {
                    if (GBS::STATUS_MISC_PLLAD_LOCK::read() == 1) {
                        lockCounter++;
                        break;
                    }
                }
            }
        }
    }
    lockCounterPrevious = getMovingAverage(lockCounter);
}

void stopWire()
{
    pinMode(SCL, INPUT);
    pinMode(SDA, INPUT);
    ESP.wdtFeed();
    delayMicroseconds(80);
}

void startWire()
{
    Wire.begin();

    pinMode(SCL, OUTPUT_OPEN_DRAIN);
    pinMode(SDA, OUTPUT_OPEN_DRAIN);

    // Wire.setClock(400000);
}


#if GBS_DEBUG
// The whole ADC sampling group, applied the way the firmware applies it, so an
// experiment over it costs a request rather than a flash.
//
// **THE GROUP CANNOT BE BISECTED BY HAND.** PLLAD_MD, KS, CKOS, ICP, FS and the
// two decimators are one setting: PLLAD_LAT loads several of them on a rising
// edge and the loop filter has to suit the tap, so writing two or three of them
// over /setreg leaves the PLL unlocked in a state that locked beforehand. This
// goes through the same call the switch does, and moves the channel's played-out
// raster with the divider, which is the other half a hand sweep gets wrong.
//
// Pass-through only. On the scaling path the divider belongs to the engine,
// which re-solves it from the measurement and would take this straight back.
static void reportSampleClock(const char *what)
{
    debugPrintf("sample clock %s: MD %u KS %u CKOS %u DEC2_BYPS %u "
                "HSYNC_RST %u HTOTAL %u lock %u\n",
                what,
                (unsigned)GBS::PLLAD_MD::read(), (unsigned)GBS::PLLAD_KS::read(),
                (unsigned)Tv5725::Adc::PLLAD_CKOS::read(),
                (unsigned)Tv5725::Adc::DEC2_BYPS::read(),
                (unsigned)GBS::HD_HSYNC_RST::read(),
                (unsigned)GBS::STATUS_SYNC_PROC_HTOTAL::read(),
                (unsigned)GBS::STATUS_MISC_PLLAD_LOCK::read());
}

// The scaling path's sampling group, written the way the engine writes it: the
// divider is one quantity in three registers and PLLAD_LAT loads several
// members of the ADC PLL group on one edge, so writing a subset by hand leaves
// the PLL unlocked at a value every register reports correctly.
static void applyCoastOverride(bool apply, bool clear, uint8_t pre, uint8_t post)
{
    if (apply) {
        if (clear)
            Tv5725::SyncProcessor::forgetCoastOverride();
        else
            Tv5725::SyncProcessor::overrideCoast(pre, post);

        // The pair reaches the chip only through a sync-type application, which
        // skips a path already in force, so a re-solve alone leaves a settled
        // source on the old coast. The branch comes from the sync type the
        // engine holds: SyncMeasurement's is not settled while detection probes.
        geometry.reapplySyncTypeInForce();
        inputAcquisition.resolveFromSource();
    }

    debugPrintf("coast: %s %u/%u\n",
        Tv5725::SyncProcessor::coastOverridden() ? "override" : "default",
        (unsigned)Tv5725::SyncProcessor::preCoastLines(),
        (unsigned)Tv5725::SyncProcessor::postCoastLines());
}

static void applySampleClock(bool apply, uint16_t divider, uint8_t oversample)
{
    if (!apply) {
        reportSampleClock("now");
        return;
    }

    const bool passingThrough = Tv5725::VideoRoute::isHdBypassChannel();
    const uint32_t lineRateHz = sourceSampling.lineRateHz();
    const uint8_t ratio =
        oversample != 0 ? oversample : Tv5725::Adc::OversampleAsClockAllows;
    const uint16_t wanted =
        divider != 0 ? divider
                     : (passingThrough
                            ? Tv5725::HdBypass::dividerFor(lineRateHz)
                            : Tv5725::SamplingClock::recommendedDivider(
                                  lineRateHz, ratio, geometry.lineDoubled()));

    if (wanted == 0) {
        debugPrintf("sample clock: no line rate measured, nothing applied\n");
        return;
    }

    if (passingThrough)
        Tv5725::HdBypass::applyPassThroughSampling(wanted, lineRateHz,
                                                   sourceSampling.hsync(), ratio);
    else
        geometry.applyChosenSampling(wanted, ratio);

    // Writing the group is not enough to re-establish lock: the PLL and the
    // phase adjusters have to be restarted after it, and without that the ADC
    // PLL stays out of lock at whatever was written -- measured, including when
    // the value written is the one it already held.
    restartAfterBypassSwitch();

    reportSampleClock("applied");
}

// Hand the engine a divider to solve AROUND, and re-solve. What this buys over
// applySampleClock() is that the capture window, both scales, the fetch and the
// stride are computed for the divider rather than left describing the previous
// solve -- so two densities are comparable as pictures. Held until released
// with 0, and a source mode change while it is held solves the new rate around
// the old divider.
// docs/investigations/a-hand-set-divider-cannot-be-judged-against-a-solved-window.md
static void holdSampleClock(uint16_t divider)
{
    geometry.holdDivider(divider);
    if (!geometry.resolve()) {
        debugPrintf("divider hold %u: solve refused\n", (unsigned)divider);
        return;
    }
    debugPrintf("divider hold %u: IF_HSYNC_RST %u HB_ST2 %u HSCALE %u VSCALE %u\n",
                (unsigned)divider,
                (unsigned)GBS::IF_HSYNC_RST::read(),
                (unsigned)GBS::IF_HB_ST2::read(),
                (unsigned)GBS::VDS_HSCALE::read(),
                (unsigned)GBS::VDS_VSCALE::read());
    reportSampleClock(divider != 0 ? "held" : "released");
}

// WHERE A SIGNAL REACHES, which no register value can answer. TEST_BUS_SEL picks
// which block drives DEBUG_IN_PIN, and the transition count over one window
// separates a field-rate signal from a line-rate one and both from a dead bus:
// at 50 Hz expect single digits, at 15.6 kHz several hundred.
//
// THE HIGH COUNT IS WHAT SAYS WHICH WAY UP. A sync pulse cannot be more than
// half the interval and still leave a raster, so the shorter state IS the pulse
// and the duty states the polarity of the wire rather than of the separator.
// One sample is 2.5 us against a 3.2 us pulse, which resolves no single pulse
// at all -- the RATIO is what converges, over the hundreds of lines a window
// holds. docs/source-identity-and-framing-lookup.md
//
// SP_TEST_MODULE exposes one sync-processor stage (4 is vs_act_det, 6 the
// retiming module, 7 out proc) and IF_TEST_SEL one input-formatter signal, so a
// sweep taken on each sync type says which stage stops carrying vertical sync.
// A stage carries several signals, so the signal is asked for too: selecting a
// module alone reports whichever signal the last caller left.
static void sweepTestBus(uint16_t windowMs, uint8_t spModule, uint8_t spSignal,
                         uint8_t ifSel)
{
    const Tv5725::TestBus::Hold held;

    if (spModule != 0xff)
        Tv5725::SyncProcessor::driveTestBus(spModule, spSignal);
    if (ifSel != 0xff)
        Tv5725::TestBus::driveFormatter(ifSel);
    Tv5725::TestBus::enable(true);

    debugPrintf("tb,header,sel,transitions,high,spins,first,last ms=%u sp=%d sig=%u if=%d sogmode=%d\n",
           (unsigned)windowMs, (int)(int8_t)spModule, (unsigned)spSignal,
           (int)(int8_t)ifSel, (int)GBS::SP_SOG_MODE::read());

    for (uint8_t sel = 0; sel < 32; sel++) {
        Tv5725::TestBus::select(sel);
        delay(1);

        int level = digitalRead(DEBUG_IN_PIN);
        const int first = level;
        uint32_t transitions = 0;
        uint32_t high = 0;
        uint32_t spins = 0;
        const uint32_t deadline = millis() + windowMs;
        while ((int32_t)(millis() - deadline) < 0) {
            const int sample = digitalRead(DEBUG_IN_PIN);
            if (sample != level) {
                transitions++;
                level = sample;
            }
            if (sample)
                high++;
            if (++spins % 4096 == 0)
                ESP.wdtFeed();
        }
        debugPrintf("tb,%u,%u,%u,%u,%d,%d\n", (unsigned)sel, (unsigned)transitions,
               (unsigned)high, (unsigned)spins, first, level);
        handleWiFi(0);
    }

    debugPrintf("tb,done\n");
}
#endif

boolean checkBoardPower()
{
    const bool was = Tv5725::Chip::hasPower();
    if (Tv5725::Chip::checkPower())
        return 1;

    if (was) {
        Serial.println(F("! power / i2c lost !"));
    }
    return 0;
}

void calibrateAdcOffset()
{
    GBS::PLL648_CONTROL_01::write(0xA5);
    Tv5725::Adc::selectInput(2);
    Tv5725::ColourSpace::DEC_MATRIX_BYPS::write(1); 
    Tv5725::Adc::enableGainMeasurement(true);
    GBS::ADC_POWDZ::write(1);
    GBS::ADC_RYSEL_R::write(0);
    GBS::ADC_RYSEL_G::write(0);
    GBS::ADC_RYSEL_B::write(0);
    GBS::ADC_FLTR::write(3);
    GBS::ADC_TR_RSEL::write(0);
    GBS::ADC_TR_ISEL::write(0);
    GBS::SP_CS_CLP_ST::write(0x00);
    GBS::SP_CS_CLP_SP::write(0x00);
    GBS::SP_SOG_MODE::write(1);
    GBS::SP_HS2PLL_INV_REG::write(0);
    GBS::SP_CLAMP_MANUAL::write(1);
    GBS::SP_CLP_SRC_SEL::write(0);
    GBS::SP_SYNC_BYPS::write(0);
    GBS::SP_HS_PROC_INV_REG::write(0);
    GBS::SP_VS_PROC_INV_REG::write(0);
    GBS::SP_CLAMP_INV_REG::write(0);
    GBS::SP_NO_CLAMP_REG::write(0);
    GBS::SP_COAST_INV_REG::write(0);
    GBS::SP_NO_COAST_REG::write(0);
    GBS::SP_COAST_VALUE_REG::write(0);
    GBS::SP_HS_LOOP_SEL::write(0);
    GBS::SP_HS_REG::write(1);
    GBS::ADC_CLK_PA::write(2);
    GBS::ADC_CLK_PLLAD::write(0);
    GBS::ADC_CLK_ICLK2X::write(0);
    GBS::ADC_CLK_ICLK1X::write(0);
    Tv5725::TestBus::select(0x0b);
    Tv5725::Chip::resetVideoBlocks();

    uint16_t hitTargetCounter = 0;
    uint16_t readout16 = 0;
    uint8_t missTargetCounter = 0;
    uint8_t readout = 0;
    uint8_t redOffset = 0;
    uint8_t greenOffset = 0;
    uint8_t blueOffset = 0;

    Tv5725::Adc::applyGain(0x7F, 0x7F, 0x7F);
    Tv5725::Adc::applyOffset(0x7F, 0x3D, 0x7F);
    Tv5725::Adc::DEC_TEST_SEL::write(1);

    unsigned long startTimer = 0;
    for (uint8_t i = 0; i < 3; i++) {
        missTargetCounter = 0;
        hitTargetCounter = 0;
        delay(20);
        startTimer = millis();

        while ((millis() - startTimer) < 800) {
            readout16 = Tv5725::TestBus::read() & 0x7fff;

            if (readout16 < 7) {
                hitTargetCounter++;
                missTargetCounter = 0;
            } else if (missTargetCounter++ > 2) {
                if (i == 0) {
                    GBS::ADC_GOFCTRL::write(GBS::ADC_GOFCTRL::read() + 1);
                    readout = GBS::ADC_GOFCTRL::read();
                } else if (i == 1) {
                    GBS::ADC_ROFCTRL::write(GBS::ADC_ROFCTRL::read() + 1);
                    readout = GBS::ADC_ROFCTRL::read();
                } else if (i == 2) {
                    GBS::ADC_BOFCTRL::write(GBS::ADC_BOFCTRL::read() + 1);
                    readout = GBS::ADC_BOFCTRL::read();
                }

                if (readout >= 0x52) {

                    break;
                }

                delay(10);
                hitTargetCounter = 0;
                missTargetCounter = 0;
                startTimer = millis();
            }
            if (hitTargetCounter > 1500) {
                break;
            }
        }
        if (i == 0) {

            greenOffset = GBS::ADC_GOFCTRL::read();
            GBS::ADC_GOFCTRL::write(0x7F);
            GBS::ADC_ROFCTRL::write(0x3D);
            Tv5725::Adc::DEC_TEST_SEL::write(2);
        }
        if (i == 1) {
            redOffset = GBS::ADC_ROFCTRL::read();
            GBS::ADC_ROFCTRL::write(0x7F);
            GBS::ADC_BOFCTRL::write(0x3D);
            Tv5725::Adc::DEC_TEST_SEL::write(3);
        }
        if (i == 2) {
            blueOffset = GBS::ADC_BOFCTRL::read();
        }
    }

    if (readout >= 0x52) {
        redOffset = greenOffset = blueOffset = 0x40;
    }

    Tv5725::Adc::holdOffset(redOffset, greenOffset, blueOffset);

    // The measurement path this routine switched on, switched off again. The
    // auto-gain feature is its only other user and states it for itself; every
    // other borrowed field has an owner that runs without a preset load.
    // docs/investigations/what-the-adc-calibration-borrows.md
    Tv5725::Adc::enableGainMeasurement(false);
}

// What a stored output mode and pass-through preference have to tell the
// engine. Which modes exist is OutputMode's, so a name the file carried and no
// mode answers to is repaired here rather than defended in the file.
static void applyStoredSettings()
{
    if (Tv5725::OutputMode::fromName(uopt->outputResolution) == NULL)
        chooseOutputMode(&Tv5725::Mode1080p);
    applyPassThroughPreference();
}

void loadDefaultUserOptions()
{
    settings.resetScalerSettings();
    applyStoredSettings();
}

#if USE_NEW_OLED_MENU




enum EncoderState {
  STATE_00 = 0b00,  
  STATE_01 = 0b01,  
  STATE_11 = 0b11,  
  STATE_10 = 0b10   
};
volatile EncoderState encoderState = STATE_00;  
void ICACHE_RAM_ATTR isrRotaryEncoderRotateForNewMenu()
{   

    unsigned long interruptTime = millis();
    static unsigned long lastInterruptTime = 0;
    static unsigned long lastNavUpdateTime = 0;
    static OLEDMenuNav lastNav;
    EncoderState newState = static_cast<EncoderState>((digitalRead(pin_a) << 1) | digitalRead(pin_b));
    OLEDMenuNav newNav;
    switch (encoderState) {
    case STATE_00:
      if (newState == STATE_01) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::DOWN : OLEDMenuNav::UP;
      else if (newState == STATE_10) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::UP : OLEDMenuNav::DOWN;
      break;
    case STATE_01:
      if (newState == STATE_11) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::DOWN : OLEDMenuNav::UP;
      else if (newState == STATE_00) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::UP : OLEDMenuNav::DOWN;
      break;
    case STATE_11:
      if (newState == STATE_10) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::DOWN : OLEDMenuNav::UP;
      else if (newState == STATE_01) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::UP : OLEDMenuNav::DOWN;
      break;
    case STATE_10:
      if (newState == STATE_00) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::DOWN : OLEDMenuNav::UP;
      else if (newState == STATE_11) newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::UP : OLEDMenuNav::DOWN;
      break;
  }
  encoderState = newState;  // 
    if (interruptTime - lastInterruptTime > 100) 
    {   
      
              
  
        // if (!digitalRead(pin_b) && digitalRead(pin_a)) 
        // {
        //     newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::DOWN : OLEDMenuNav::UP;
        // } 
        // else if (digitalRead(pin_b) && !digitalRead(pin_a)) 
        // {
        //     newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::UP : OLEDMenuNav::DOWN;
        // }

        if ((newNav != lastNav && (interruptTime - lastNavUpdateTime < 120)) || (oled_menuItem != 0)) 
        {
            oledNav = lastNav = OLEDMenuNav::IDLE;   //
        } 
        else 
        {
            lastNav = oledNav = newNav;
            ++rotaryIsrID;
            lastNavUpdateTime = interruptTime;
        }
        lastInterruptTime = interruptTime;
    }
}

void ICACHE_RAM_ATTR isrRotaryEncoderPushForNewMenu()
{
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    if ((interruptTime - lastInterruptTime > 500) && (oled_menuItem == 0))
    {
        oledNav = OLEDMenuNav::ENTER;
        ++rotaryIsrID;
    }
    lastInterruptTime = interruptTime;
}
#endif

void discardSerialRxData()
{
    uint16_t maxThrowAway = 0x1fff;
    while (Serial.available() && maxThrowAway > 0) {
        Serial.read();
        maxThrowAway--;
    }
}

// webui.html's buttonMapping is the other half of this: the char names which
// resolution button is lit. '0' is none of them.
static char webResolutionCode(const Tv5725::OutputMode *mode)
{
    if (mode == NULL)
        return '0';
    if (mode->isBypass())
        return '8';
    if (mode == &Tv5725::Mode960p)
        return '1';
    if (mode == &Tv5725::Mode1024p)
        return '2';
    if (mode == &Tv5725::Mode720p)
        return '3';
    if (mode == &Tv5725::Mode480p)
        return '4';
    if (mode == &Tv5725::Mode1080p)
        return '5';
    if (mode == &Tv5725::Mode576p)
        return '7';
    return '0';
}

void updateWebSocketData()
{
    if (rto->webServerEnabled && rto->webServerStarted) {
        if (webSocket.connectedClients() > 0) {

            constexpr size_t MESSAGE_LEN = 6;
            char toSend[MESSAGE_LEN] = {0};
            toSend[0] = '#';

            toSend[1] = webResolutionCode(geometry.outputMode());

            if (uopt->enableFrameTimeLock) {
                toSend[4] |= (1 << 1);
            }
            if (uopt->deintMode) {
                toSend[4] |= (1 << 2);
            }
            if (uopt->wantTap6) {
                toSend[4] |= (1 << 3);
            }
            if (uopt->wantStepResponse) {
                toSend[4] |= (1 << 4);
            }

            if (uopt->enableCalibrationADC) {
                toSend[5] |= (1 << 0);
            }
            if (uopt->preferScalingRgbhv) {
                toSend[5] |= (1 << 1);
            }
            if (uopt->disableExternalClockGenerator) {
                toSend[5] |= (1 << 2);
            }

            if (ESP.getFreeHeap() > 14000) {
                webSocket.broadcastTXT(toSend, MESSAGE_LEN);
            }
            // Same reasoning as SerialMirror: skip the update, keep the client.
            // Hanging up on everyone to save 14 KB loses the session that was
            // about to tell you why the heap was low.
        }
    }
}

void handleWiFi(boolean instant)
{
    static unsigned long lastTimePing = millis();
    if (rto->webServerEnabled && rto->webServerStarted) {
        MDNS.update();
        persWM.handleWiFi();
        dnsServer.processNextRequest();

        if ((millis() - lastTimePing) > 953) {
            webSocket.broadcastPing();
        }
        if (((millis() - lastTimePing) > 973) || instant) {
            if ((webSocket.connectedClients(false) > 0) || instant) {
                updateWebSocketData();
            }
            lastTimePing = millis();
        }
    }

    if (rto->allowUpdatesOTA) {
        ArduinoOTA.handle();
    }
    yield();
}


// The acquisition path's entry gate. **THE FREEZE ONLY**: board power is a
// latched failure rather than a live reading, and it stays false through the
// whole recovery -- exactly when the engine has to solve.
// docs/video-source-acquisition.md
static bool engineMayRun()
{
#if GBS_SAMPLING_LOG
    // The divider walk writes PLLAD_MD, which the engine owns. Left running,
    // the two take turns writing it and the walk's readings are taken through
    // a divider it did not set. A monitor run is exempt: it only reads, and
    // watching a live engine is what it is for.
    if (samplingLog.sweeping())
        return false;
#endif
    return !AUTOMATION_FROZEN();
}

void setup()
{
    system_update_cpu_freq(160);

    // The engine re-establishes the sync type on every mode change, which is
    // the only signal that a source may have changed it -- a RISC PC sets it
    // from CMOS, so the mux need not have moved. docs/sync-type-selection.md
    geometry.useSyncTypeProbe(syncTypeHasOwnVsync);
    inputAcquisition.usePassThroughSwitch(enterHdBypass);
    inputAcquisition.useWatchdogFeed(feedWatchdog);
    inputAcquisition.useClock(millisNow);

    // A slew is up to 750 I2C transactions, long enough that dropping the WiFi
    // stack turns a frequency change into a reboot.
    rtos.displayClock.pumpWith(pumpWiFi);
    applyPassThroughPreference();

    // The freeze, on the tick rather than inside the engine.
    // docs/gbs-control-debug-interface.md
    inputAcquisition.useRunGate(engineMayRun);

    // delay(700);
    // ESP.wdtDisable();

    display.init();

    display.flipScreenVertically();

    irrecv.enableIRIn();
    OSD_clear();
    Osd::OSD::writeThrough(OSD_parameters);
    Osd::Panel::writeThrough(panelClear, panelLine, panelFlush);
    describedMenu.alsoDrawOn(Osd::Panel::renderer());
    PT_MUTE(0x78);
    PT_2257(70); // audible



    pinMode(pin_a, INPUT_PULLUP);
    pinMode(pin_b, INPUT_PULLUP);
    pinMode(pin_switch, INPUT_PULLUP);

#if USE_NEW_OLED_MENU
    // versatile_encoder = new Versatile_RotaryEncoder(pin_a, pin_b, pin_switch);
    // versatile_encoder->setHandleRotate(handleRotate);  //

    attachInterrupt(digitalPinToInterrupt(pin_a),   isrRotaryEncoderRotateForNewMenu, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pin_b),   isrRotaryEncoderRotateForNewMenu, CHANGE);   //isrRotaryEncoderPushForNewMenu
    attachInterrupt(digitalPinToInterrupt(pin_switch), isrRotaryEncoderPushForNewMenu, FALLING);
    initOLEDMenu();
#else
    attachInterrupt(digitalPinToInterrupt(pin_a), isrRotaryEncoder, RISING);
#endif

    rto->webServerEnabled = true;
    rto->webServerStarted = false;

    Serial.begin(115200);
    Serial.setTimeout(10);

    WiFi.hostname(device_hostname_partial);

    startWire();
    Wire.setClock(400000);
    GBS::SP_SOG_MODE::read();
    writeOneByte(0xF0, 0);
    writeOneByte(0x00, 0);
    GBS::STATUS_00::read();

    if (rto->webServerEnabled) {
        rto->allowUpdatesOTA = false;
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        WiFi.setOutputPower(16.0f);
        startWebserver();
        rto->webServerStarted = true;
    } else {
        WiFi.mode(WIFI_OFF);
        WiFi.forceSleepBegin();
    }
#ifdef HAVE_PINGER_LIBRARY
    pingLastTime = millis();
#endif

    ; // SerialMprintln(F("\nstartup"));

    loadDefaultUserOptions();

    rto->allowUpdatesOTA = false;      
    rto->freezeAutomation = false; // never persisted: a reboot returns to normal
    rto->enableDebugPings = false;     
    resetRunTimeDefaults();

    Tv5725::VideoRoute::toScaler();
    if (!rto->webServerEnabled)
        rto->webServerStarted = false;
    rto->printInfos = false;          
    rto->sourceDisconnected = true;   
    rto->isInLowPowerMode = false;    
    rto->applyPresetDoneStage = 0;     
    Tv5725::SyncProcessor::forgetPositions();
    Tv5725::SyncMeasurement::forget();
    Tv5725::SyncOnGreen::choose(5);          

    Tv5725::Adc::forgetGain();
    Tv5725::Adc::forgetOffset();

    serialCommand = '@';
    userCommand = '@';

    pinMode(DEBUG_IN_PIN, INPUT);
    pinMode(15, OUTPUT);

    display.init();
    display.flipScreenVertically();

    // Start-up Logo
    unsigned long initDelay = millis();
    while (millis() - initDelay < 1500) {
        display.drawXbm(2, 2, gbsicon_width, gbsicon_height, gbsicon_bits);
        display.display();
        handleWiFi(0);
        delay(1);
    }
    display.clear();

    Tv5725::BringUp::holdAllBlocks();
    GBS::PLLAD_VCORST::write(1);
    GBS::PLLAD_PDZ::write(0);

    // The reset reason is the one fact that says whether this was a real
    // power-up or a reset with the rails still charged. Without it a bench cold
    // boot and an RTS reset are indistinguishable in the log, and they are not
    // the same test -- the failure being chased only shows up on the former.
    bootLogPrintf("BOOT: reason='%s' t=%lums\n",
        ESP.getResetReason().c_str(), (unsigned long)millis());

    const uint32_t fsBeginStart = millis();
    const bool fsUp = LittleFS.begin();
    bootLogPrintf("PREFS: LittleFS.begin()=%d at t=%lums (took %lums)\n",
        fsUp ? 1 : 0, (unsigned long)millis(),
        (unsigned long)(millis() - fsBeginStart));

    if (!fsUp) {
        bootLogPrintf("PREFS: LittleFS mount FAILED -- running on defaults, will not save\n");
        prefsAreSuspect = true;
    } else {
        // Wait for the file to be READABLE, not merely openable.
        //
        // A flash awake enough to serve metadata and not yet awake enough to
        // serve content satisfies the open and the size on the first attempt
        // and hands the parser nothing. What says the file arrived whole is its
        // last line, so the retry is on reaching the terminator -- which works
        // at any length, where a byte count only worked while every build wrote
        // the same number of them.
        //
        // Ten attempts at 100 ms is a second of patience before giving up, and
        // costs nothing on a healthy boot where the first attempt succeeds.
        bool settingsReadable = false;

        bootLogPrintf("PREFS: exists=%d t=%lums\n",
            LittleFS.exists(SettingsFilePath) ? 1 : 0, (unsigned long)millis());

        for (uint8_t attempt = 0; attempt < 10 && !settingsReadable; attempt++) {
            const uint32_t attemptStart = millis();
            File f = LittleFS.open(SettingsFilePath, "r");
            if (!f) {
                bootLogPrintf("PREFS: attempt %u t=%lums open=0\n",
                    (unsigned)attempt + 1, (unsigned long)attemptStart);
                delay(100);
                continue;
            }

            // Every attempt starts from the defaults, so a part-applied read is
            // never what the next one builds on.
            settings.defaults();

            uint16_t applied = 0;
            char line[80];
            while (f.available() && !settingsReadable) {
                const String next = f.readStringUntil('\n');
                strncpy(line, next.c_str(), sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
                switch (settings.readLine(line)) {
                    case Prefs::Settings::Applied:
                        ++applied;
                        break;
                    case Prefs::Settings::End:
                        settingsReadable = true;
                        break;
                    default:
                        break;
                }
            }

            // One call, not three. Split across separate printfs the tail of
            // this line reached the serial cable but not the buffer, which
            // dropped the evidence that distinguishes a file holding defaults
            // from a read that failed.
            bootLogPrintf("PREFS: attempt %u t=%lums size=%u applied=%u end=%d\n",
                (unsigned)attempt + 1, (unsigned long)attemptStart,
                (unsigned)f.size(), (unsigned)applied,
                settingsReadable ? 1 : 0);
            f.close();

            if (!settingsReadable)
                delay(100);
        }

        if (!settingsReadable && LittleFS.exists(SettingsFilePath)) {
            // The file is there but will not come back whole. Defaults would be
            // wrong, and writing them destroys the settings still on flash. Run
            // on the defaults and touch nothing: a bad boot the user can power
            // cycle out of beats a good boot with their settings gone.
            bootLogPrintf("PREFS: UNREADABLE after 10 attempts; NOT overwriting them\n");
            settings.defaults();
            prefsAreSuspect = true;
        } else if (!settingsReadable) {
            bootLogPrintf("PREFS: no file yet, creating\n");
            settings.defaults();
        }

        // Nothing to read is not a failed read: the defaults are written out so
        // the next boot has a file, and the save a user's first change makes is
        // not refused.
        const bool creating = !settingsReadable && !prefsAreSuspect;
        applyStoredSettings();
        if (creating)
            saveUserPrefs();

        // The one line worth reading on a cold boot. 1920x1080 with
        // frameTimeLock 0 is the defaults signature -- if it shows up here while
        // suspect=0, the read passed validation and still produced defaults, and
        // the guard above has another hole in it.
        bootLogPrintf("PREFS: loaded output=%s frameTimeLock=%u slot=%u "
               "SeleInputSource=%u suspect=%d t=%lums\n",
            uopt->outputResolution, (unsigned)uopt->enableFrameTimeLock,
            (unsigned)uopt->presetSlot, (unsigned)SeleInputSource,
            prefsAreSuspect ? 1 : 0, (unsigned long)millis());

        // The framing table, from its own file. After the preferences, so its
        // own read failure is distinguishable in the boot log from theirs.
        loadFramingTable();
        bootLogPrintf("FRAMING: %u stored, suspect=%d t=%lums\n",
            (unsigned)sourceFramings.count(), framingIsSuspect ? 1 : 0,
            (unsigned long)millis());
    }


    GBS::PAD_CKIN_ENZ::write(1);
    externalClockGenDetectAndInitialize();

    startWire();
    GBS::STATUS_00::read();
    GBS::STATUS_00::read();
    GBS::STATUS_00::read();

    initDelay = millis();
    while (millis() - initDelay < 1000) {
        handleWiFi(0);
        delay(1);
    }

    if (WiFi.status() == WL_CONNECTED) {
    } else if (WiFi.SSID().length() == 0) {
        ; // SerialMprintln(FPSTR(ap_info_string));
    } else {
        ; // SerialMprintln(F("(WiFi): still connecting.."));
        WiFi.reconnect();
    }

    GBS::STATUS_00::read();
    GBS::STATUS_00::read();
    GBS::STATUS_00::read();

    boolean powerOrWireIssue = 0;
    if (!checkBoardPower()) {
        stopWire();
        for (int i = 0; i < 40; i++) {

            startWire();
            GBS::STATUS_00::read();
            digitalWrite(SCL, 0);
            // ESP.wdtFeed();
            delayMicroseconds(12);
            stopWire();
            if (digitalRead(SDA) == 1) {
                break;
            }
            if ((i % 7) == 0) {
                delay(1);
            }
        }

        startWire();
        delay(1);
        GBS::STATUS_00::read();
        delay(1);

        if (!checkBoardPower()) {
            stopWire();
            powerOrWireIssue = 1;
            rto->syncWatcherEnabled = false;
        } else {
            rto->syncWatcherEnabled = true;
            ; // SerialMprintln(F("recovered"));
        }
    }

    if (powerOrWireIssue == 0) {

        if (!rto->displayClock.driving()) {
            externalClockGenDetectAndInitialize();
        }
        if (rto->displayClock.driving()) {
            Serial.println(F("ext clockgen detected"));
        } else {
            Serial.println(F("no ext clockgen"));
        }

        zeroAll();
        setResetParameters();

        // BEFORE detection, which runs below and cannot measure a source
        // through a sync processor left at zeros: with the coast and the delta
        // registers clear, STATUS_SYNC_PROC_HTOTAL reads a number that does not
        // move when the divider is written and latched by hand. The full
        // bring-up runs after the last setResetParameters(), which is too late
        // for this, and this block survives it -- SFTRST_SYNC_RSTZ is not one of
        // the six that call holds.
        Tv5725::SyncProcessor::init();
        prepareSyncProcessor();

        uint8_t productId = GBS::CHIP_ID_PRODUCT::read();
        uint8_t revisionId = GBS::CHIP_ID_REVISION::read();
        ; // SerialMprint(F("Chip ID: "));
        ; // SerialMprint(productId, HEX);
        ; // SerialMprint(" ");
        ; // SerialMprintln(revisionId, HEX);

        if (uopt->enableCalibrationADC) {

            calibrateAdcOffset();
        }
        setResetParameters();

        // After calibrateAdcOffset(), which parks ADC_INPUT_SEL on 2, and after
        // setResetParameters(), so nothing here overwrites it before detection
        // gets a look.
        applySavedInputSource();

        // After the last setResetParameters(), which holds six blocks in reset:
        // a bring-up that ran before it would be discarded.
        Tv5725::BringUp::init(inputFormatter);

        // The output the user chose, handed over at boot rather than only from
        // inside a preset load. VideoSourceAcquisition::sourceMoved() cannot
        // arm a solve without a resolution to solve to, and the only other
        // armer IS that preset load -- so a boot whose detection pass is
        // refused had no route to a picture for the life of the boot.
        inputAcquisition.setOutputResolution(chosenOutputMode());

        delay(4);
        handleWiFi(1);
        delay(4);
    } else {
        ; // SerialMprintln(F("Please check board power and cabling or restart!"));
    }

    if (Serial.available()) {
        discardSerialRxData();
    }
    halfPeriod.reset();
}

// The source-recovery poll: while the firmware believes nothing is plugged in,
// re-run detection every 500 ms and step the SOG slice level down, sweeping for
// a level that finds sync.
//
// loop() reaches this directly rather than through the acquisition tick, so the
// freeze has to be checked here as well. Guarding detectAndSwitchToActiveInput()
// instead does not work: frozen it returns 0, which is what tells
// inputAndSyncDetect() nothing is plugged in.
//
// lastTimeSourceCheck is shared with the sync-present branch in loop() so the
// two cadences cannot drift apart.
void runSourceRecovery(unsigned long &lastTimeSourceCheck)
{
    // Frozen: docs/gbs-control-debug-interface.md
    if (AUTOMATION_FROZEN()) {
        return;
    }
    if ((millis() - lastTimeSourceCheck) < 500) {
        return;
    }

    if (checkBoardPower()) {
        inputAndSyncDetect();
    } else {
        rto->syncWatcherEnabled = false;
    }
    lastTimeSourceCheck = millis();

    const uint8_t currentSOG = Tv5725::SyncOnGreen::level();
    Tv5725::SyncOnGreen::apply(currentSOG >= 3 ? currentSOG - 1 : 6);
}

// Passes with nothing measured before the rails are questioned rather than the
// source. Far past every recovery the ladder runs, so it only fires on a run
// that none of them fixed.
static const uint16_t BoardPowerCheckPass = 61;

void loop()
{
#if GBS_DEBUG
    serviceRegisterQueue();
#endif

    // At the top of loop() and nowhere else. Inside web_service()'s timed block
    // the samples came 300 ms apart with a 5 ms interval asked for, which is
    // coarser than the HTTP polling this exists to beat.
#if GBS_SAMPLING_LOG
    samplingLog.poll(millis());
#endif

    // Hand the boot backlog to the first console that attaches, so the web
    // console opens on the whole boot rather than starting mid-sentence.
    //
    // Polled rather than driven by an onEvent() callback because none is
    // registered -- setup() only calls webSocket.begin(). A poll costs one
    // comparison and avoids introducing a callback nothing else uses.
#if BOOTLOG_BYTES > 0
    if (!bootLogDelivered && bootLogLen > 0 && webSocket.connectedClients() > 0) {
        bootLogDelivered = true; // set first: broadcastTXT re-enters SerialMirror

        // **SEND IT IN PIECES, NOT ONE 2 KB FRAME.** A single broadcast of the
        // whole backlog allocates for every connected client at once, and
        // SerialMirror drops clients when free heap falls below its threshold
        // during a console write -- so the replay hangs up the very client it is
        // replaying to. Chunked with a yield between, and skipped entirely when
        // the heap is already tight: losing the backlog is a smaller loss than
        // losing the console session it was meant to enrich.
        const uint16_t chunk = 256;
        for (uint16_t sent = 0; sent < bootLogLen; sent += chunk) {
            if (ESP.getFreeHeap() < 22000) {
                Serial.println(F("BOOTLOG: heap low, replay truncated"));
                break;
            }
            uint16_t n = bootLogLen - sent;
            if (n > chunk) {
                n = chunk;
            }
            webSocket.broadcastTXT((const char *)bootLog + sent, n);
            yield();
        }
    }
#endif

    static uint8_t readout = 0;
    static uint8_t segmentCurrent = 255;
    static uint8_t registerCurrent = 255;
    static uint8_t inputToogleBit = 0;
    static uint8_t inputStage = 0;
    static unsigned long lastTimeSourceCheck = 500;
    static unsigned long lastTimeCheck = 500;
    static unsigned long lastTimeInterruptClear = millis();
    // static unsigned long lastTim_signal;
    // static unsigned long lastTim_sys;
    // static unsigned long lastTim_web;
    // Tim_signal = millis();
    // Tim_sys = millis();
    // Tim_web = millis();
    static bool dir = 0;

    // static unsigned long OneSec = millis();
    // if (millis() - OneSec >= 3000)
    //   {
    //     // printf("bit[0]:%d  \n",GBS::PAD_CKIN_ENZ::read());
    //     // printf("bit[4 3 1 0]:%d %d %d %d \n",GBS::PAD_TRI_ENZ::read(),GBS::PAD_BLK_OUT_ENZ::read(),GBS::PAD_CKOUT_ENZ::read(),GBS::PAD_CKIN_ENZ::read());
    //     OneSec = millis();
    //   }

    // versatile_encoder->ReadEncoder();

    // How slow a loop pass gets between frames: an IR frame arriving while the
    // receiver has not been resumed is dropped, so the pass time is the thing
    // that decides whether a press survives.
    static unsigned long irLoopMark = 0;
    unsigned long irLoopNow = millis();
    if (irLoopMark != 0 && (irLoopNow - irLoopMark) > irWorstLoopMs)
        irWorstLoopMs = irLoopNow - irLoopMark;
    irLoopMark = irLoopNow;

    // Which consumer took the frame, counted rather than named: every decode
    // site goes through IrReceiver, so this needs no edit at any of them.
    int irMenuBefore = oled_menuItem;
    uint32_t irBefore = irrecv.decodes();
    pressDescribedMenuFromRemote();
    drawOverlayScreens();
    uint32_t irAfterSelect = irrecv.decodes();
    OSD_IR();

    // Two trees draw the panel, so whichever is open owns it: the icon tree's
    // tick() would otherwise paint over a described page.
    if (oled_menuItem == 0)
        NEW_OLED_MENU = !describedMenu.isOpen();
    traceIrFrames(irAfterSelect - irBefore, irrecv.decodes() - irAfterSelect,
                  irMenuBefore);

    uint8_t oldIsrID = rotaryIsrID;
    if (NEW_OLED_MENU == true) {
        oledMenu.tick(oledNav);
        if (oldIsrID == rotaryIsrID) {
            oledNav = OLEDMenuNav::IDLE;
        }
    }

    if ((millis() - Tim_sys) >= 400) {
        PT_2257(Volume + 12);
        Tim_sys = millis();
    }

    handleWiFi(0);

    if ((millis() - Tim_signal) >= 100) {
        Mode_Option();
        Tim_signal = millis();
    }
    web_service(inputStage, segmentCurrent, registerCurrent, readout, inputToogleBit);

    // Once the press has acted, not before. A press only moves the cursor and
    // queues a letter, and what a row says is read from whoever holds the
    // value, so a page drawn while the queue is still full shows every value
    // row as it was one press ago. Held until it drains rather than drawn
    // twice: a redraw flushes the panel's whole framebuffer, and a second one
    // per press slows loop() enough to make presses coalesce. Only while the
    // chain's menu is closed, or the two paint over each other.
    if (oled_menuItem == 0 && !menuCommandPending())
        describedMenu.drawIfNeeded();

          
    if (rto->syncWatcherEnabled && Tv5725::Chip::hasPower()) {
        if ((millis() - lastTimeInterruptClear) > 3000) {
            Tv5725::Interrupts::acknowledgeAllButSogBad();
            lastTimeInterruptClear = millis();
        }
    }

    if (rto->printInfos == true) {
        printInfo();
    }

    // Deliberately ahead of every gate below: a framing the user requested is
    // not automation, so neither freeze nor a disabled sync watcher may hold
    // it, and a mode change must finish even when the sync watcher is off.
    //
    // The engine decides when the source has settled into a new mode, because
    // it owns the measurements that decide it. Cheap on every pass, and
    // expensive only while a change is outstanding.

    pollFramingSave(millis());

    // What the acquisition layer is told rather than measures: the user's
    // deinterlacer preferences, and whether keeping the source coming is wanted
    // at all -- detection owns the input while a source is disconnected, and
    // the automatic path can be switched off. Told every pass rather than at
    // every writer, so neither can go stale.
    inputAcquisition.allowMaintenance(!rto->sourceDisconnected
                                      && rto->syncWatcherEnabled);
    {
        Tv5725::Deinterlacer::Preferences wanted;
        wanted.automatic = rto->deinterlaceAutoEnabled;
        wanted.bob = uopt->deintMode == 1;
        wanted.scanlines = uopt->wantScanlines;
        wanted.scanlineStrength = uopt->scanlineStrength;
        wanted.relockable = uopt->enableFrameTimeLock || rto->displayClock.driving();
        Tv5725::Deinterlacer::choose(wanted);
    }

    if (inputAcquisition.poll(millis())) {
        // The user's picture options, stated again because the mode change
        // reset the video blocks that carry them. They cannot live with the
        // bring-up, which has no way to know what was chosen -- so they are
        // the one part of the setup the sketch still has to supply, and a mode
        // change reached without a preset load would otherwise leave the
        // peaking and the line filter at nothing.
        // docs/investigations/an-acquisition-without-a-preset-load-emits-a-flat-field.md
        geometry.applyPictureFilters(uopt->wantVdsLineFilter, uopt->wantPeaking);
        geometry.applyOutputPictureFilters(uopt->wantSharpness,
                                           uopt->wantStepResponse);

        // Rate steer last, after raster, clock and windows. The solve moved the
        // raster, so the ratio the frequency lock steers by is stale -- and
        // re-establishing it here is the only thing that does: the
        // applyPresetDoneStage block fires once and cannot see a later solve.
        frameSync.clearFrequency();
        frameSync.matchRate(sourceSampling.settledFieldRateHz());

    }

    // What a pass decided that lives above the acquisition layer: the frame
    // time lock and the external clock generator. Reported rather than
    // injected, which is what keeps the layer free of uopt and of FrameSync.
    {
        const VideoSourceAcquisition::Report &report = inputAcquisition.report();
        if (report.frameTimingMoved)
            frameSync.reset(uopt->frameTimeLockMethod);
        if (report.vsyncLockStale)
            frameSync.defer(millis());
        if (report.outputRateSettled)
            frameSync.matchRate(sourceSampling.settledFieldRateHz());
    }

    // On the pass that advanced the run, not on a timer of its own: every
    // threshold below counts in those passes, and two 20 ms cadences beside each
    // other drift until a count is answered twice or not at all.
    if (rto->sourceDisconnected == false && rto->syncWatcherEnabled == true
        && inputAcquisition.runAdvanced()) {
        if (uopt->enableAutoGain == 1 && !rto->sourceDisconnected && inputAcquisition.sourceIsPresent() && Tv5725::SyncProcessor::clampPlaced() && inputAcquisition.acquiredPasses() > 90 && Tv5725::Chip::hasPower()) {
            if (Tv5725::Adc::dividerLatched(Tv5725::SyncProcessor::lineSamples())) {
                const Tv5725::TestBus::Hold held;
                Tv5725::Adc::DEC_TEST_SEL::write(1);
                Tv5725::TestBus::select(0xb);
                if (GBS::STATUS_INT_SOG_BAD::read() == 0) {
                    runAutoGain();
                }
            }
        }
    }

    {
        FrameTimeLock::Conditions conditions;
        conditions.optionEnabled = uopt->enableFrameTimeLock;
        conditions.sourcePresent = !rto->sourceDisconnected;
        conditions.syncWatcherEnabled = rto->syncWatcherEnabled;
        conditions.method = uopt->frameTimeLockMethod;
        frameTimeLock.service(conditions, millis());
    }

    if (geometry.scalerCarriesVideo() && rto->syncWatcherEnabled
        && !Tv5725::SyncProcessor::coastPlaced()) {
        if (inputAcquisition.acquiredPasses() >= 7) {
            if (inputAcquisition.sourceIsPresent()) {
                inputAcquisition.placeCoastWindow(0);
                if (Tv5725::SyncProcessor::coastPlaced()) {
                    if (sourceHasSerratedSync()) 
                    {

                        Tv5725::SyncProcessor::setSubCoast(true);
                        Tv5725::SyncProcessor::setHsyncOverflowProtect(false);
                    }
                }
            }
        }
    }

    if (inputAcquisition.sourceIsPresent() && (inputAcquisition.acquiredPasses() >= 4) &&
        !Tv5725::SyncProcessor::clampPlaced() && rto->syncWatcherEnabled) {
        inputAcquisition.placeClampWindow();
        if (Tv5725::SyncProcessor::clampPlaced()) {
            if (Tv5725::SyncProcessor::clampHeld()) {
                Tv5725::SyncProcessor::releaseClamp();
            }
        }
    }


    if ((rto->applyPresetDoneStage == 1) &&
        ((inputAcquisition.acquiredPasses() > 35 && inputAcquisition.acquiredPasses() < 45) ||
         !rto->syncWatcherEnabled)) {
        if (!rto->syncWatcherEnabled) {
            inputAcquisition.placeClampWindow();
            Tv5725::SyncProcessor::releaseClamp();
        }

        if (rto->displayClock.driving()) {
            if (!Tv5725::VideoRoute::isHdBypassChannel())
                rto->displayClock.handOver();
            frameSync.matchRate(sourceSampling.settledFieldRateHz());
        }
        rto->applyPresetDoneStage = 0;
    }
    else if (rto->applyPresetDoneStage == 1 && (inputAcquisition.acquiredPasses() > 35)) {
        frameSync.matchRate(sourceSampling.settledFieldRateHz());
        rto->applyPresetDoneStage = 0;
    }

    if (rto->syncWatcherEnabled == true && rto->sourceDisconnected == true && Tv5725::Chip::hasPower()) {
        runSourceRecovery(lastTimeSourceCheck);
    }

    // A run this long with nothing measured is worth one I2C probe to tell a
    // source that went away from a board that lost its rails. One position
    // rather than two: the pair existed so the second could be latched out.
    if (inputAcquisition.unmeasuredPasses() == BoardPowerCheckPass && Tv5725::Chip::hasPower()
        && !checkBoardPower()) {
        stopWire();
    }

    if (!Tv5725::Chip::hasPower() && rto->syncWatcherEnabled) 
    {
        if (digitalRead(SCL) && digitalRead(SDA)) {
            delay(50);
            if (digitalRead(SCL) && digitalRead(SDA)) {
                Serial.println(F("power good"));
                delay(350);
                startWire();
                {

                    GBS::SP_SOG_MODE::read();
                    GBS::SP_SOG_MODE::read();
                    writeOneByte(0xF0, 0);
                    writeOneByte(0x00, 0);
                    GBS::STATUS_00::read();
                }
                rto->syncWatcherEnabled = true;
                Tv5725::Chip::holdPower(true);
                delay(100);
                goLowPowerWithInputDetection();
            }
        }
    }

#ifdef HAVE_PINGER_LIBRARY

    if (WiFi.status() == WL_CONNECTED) {
        if (rto->enableDebugPings && millis() - pingLastTime > 1000) {

            if (pinger.Ping(WiFi.gatewayIP(), 1, 750) == false) {
                Serial.println("Error during last ping command.");
            }
            pingLastTime = millis();
        }
    }
#endif
}
/////////
/////////
/////////
/////////
/////////
/////////
/////////
//////////////////
/////////
/////////
/////////
/////////
/////////
/////////
//////////////////
/////////
/////////
/////////
/////////
/////////
/////////
//////////////////
/////////
/////////
/////////
/////////
/////////
/////////
//////////////////
/////////
/////////
/////////
/////////
/////////
/////////
//////////////////
/////////
/////////
/////////
/////////
/////////
/////////
//////////////////
/////////
/////////
/////////
/////////
/////////
/////////
/////////

// What a press asks for in output pixels: what /sc?<pad>=<n> named, or the pad's
// own step.
static int16_t pressStep(int16_t asked, int16_t step)
{
    return asked != 0 ? asked : step;
}

void web_service(uint8_t inputStage, uint8_t segmentCurrent, uint8_t registerCurrent, uint8_t readout, uint8_t inputToogleBit)
{


    if ((millis() - Tim_web) >= 300) {
        if (Serial.available()) {
            serialCommand = Serial.read();
        } else if (inputStage > 0) {
            ; // SerialMprintln(F(" abort"));
            discardSerialRxData();
            serialCommand = ' ';
        }
        if (serialCommand != '@') {
            const int16_t pressPixels = serialCommandPixels;
            serialCommandPixels = 0;

            if (inputStage > 0) {
                if (serialCommand != 's' && serialCommand != 't' && serialCommand != 'g') {
                    discardSerialRxData();
                    ; // SerialMprintln(F(" abort"));
                    serialCommand = ' ';
                }
            }

            switch (serialCommand) {
                case ' ':

                    inputStage = segmentCurrent = registerCurrent = 0;
                    break;
                case 'd': {

                    disableScanlines();

                    if (uopt->enableFrameTimeLock && frameSync.lastCorrection() != 0) {
                        frameSync.reset(uopt->frameTimeLockMethod);
                    }

                    for (int segment = 0; segment <= 5; segment++) {
                        dumpRegisters(segment);
                    }; // SerialMprintln("};");
                } break;
                // The four pads. Every press recomputes every window from the
                // capture and the raster. docs/firmware-geometry-engine.md
                case '+':
                    geometryControls.horizontalPan(+pressStep(pressPixels, Tv5725::ControlSteps::Pan));
                    break;
                case '-':
                    geometryControls.horizontalPan(-pressStep(pressPixels, Tv5725::ControlSteps::Pan));
                    break;
                case '*':
                    geometryControls.verticalPan(-pressStep(pressPixels, Tv5725::ControlSteps::Pan));
                    break;
                case '/':
                    geometryControls.verticalPan(+pressStep(pressPixels, Tv5725::ControlSteps::Pan));
                    break;
                // Zoom in is a negative delta: it crops, and the scale follows.
                case 'z':
                    geometryControls.horizontalZoom(+pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    geometryControls.verticalZoom(+pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    break;
                case 'h':
                    geometryControls.horizontalZoom(-pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    geometryControls.verticalZoom(-pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    break;
                // Horizontal zoom on its own, the mirror of '4'/'5' vertically.
                case 'I':
                    geometryControls.horizontalZoom(+pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    break;
                case 'O':
                    geometryControls.horizontalZoom(-pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    break;
                // Re-derive every register from the framing held and the source
                // as it reads now, without moving the framing.
                case 'U':
                    inputAcquisition.resolveFromSource();
                    break;
                // Back to the default framing. The framing is the engine's own
                // state and no register holds it, so without this a picture
                // zoomed into a corner needs a mode change or a reboot.
                case 'B':
                    geometry.reset();
                    break;
                case 'q':
                    Tv5725::Chip::resetVideoBlocks();
                    delay(2);
                    Tv5725::MemoryBus::restart();
                    delay(2);
                    Tv5725::Adc::restartPhaseAdjusters();
                    break;
                case 'D':; // SerialMprint(F("debug view: "));
                    if (GBS::ADC_UNUSED_62::read() == 0x00) {
                        GBS::VDS_PK_LB_GAIN::write(0x3f);
                        GBS::VDS_PK_LH_GAIN::write(0x3f);
                        GBS::ADC_UNUSED_61::write(GBS::HD_Y_OFFSET::read());
                        GBS::ADC_UNUSED_62::write(1);
                        GBS::VDS_Y_OFST::write(GBS::VDS_Y_OFST::read() + 0x24);
                        GBS::HD_Y_OFFSET::write(GBS::HD_Y_OFFSET::read() + 0x24);
                        if (!Tv5725::Adc::inputIsComponent()) {

                            GBS::HD_DYN_BYPS::write(0);
                            GBS::HD_U_OFFSET::write(GBS::HD_U_OFFSET::read() + 0x24);
                            GBS::HD_V_OFFSET::write(GBS::HD_V_OFFSET::read() + 0x24);
                        }; // SerialMprintln("on");
                    } else {
                        geometry.applyOutputPictureFilters(
                            uopt->wantSharpness, uopt->wantStepResponse);
                        // The luma offset is the balance's, so leaving the view
                        // asks it rather than putting back a saved copy.
                        applyColourBalance();
                        GBS::HD_Y_OFFSET::write(GBS::ADC_UNUSED_61::read());
                        if (!Tv5725::Adc::inputIsComponent()) {

                            GBS::HD_DYN_BYPS::write(1);
                            GBS::HD_U_OFFSET::write(0);
                            GBS::HD_V_OFFSET::write(0);
                        }

                        GBS::ADC_UNUSED_61::write(0);
                        GBS::ADC_UNUSED_62::write(0);
                        ; // SerialMprintln("off");
                    }
                    serialCommand = '@';
                    break;
                case 'C':; // SerialMprintln(F("PLL: ICLK"));

                    GBS::PLL648_CONTROL_01::write(0x85);
                    GBS::PLL_CKIS::write(1);
                    Tv5725::Adc::latch();

                    frameTimeLock.forgiveFailures();
                    frameSync.reset(uopt->frameTimeLockMethod);

                    delay(200);
                    break;
                case 'P':; // SerialMprint(F("auto deinterlace: "));
                    rto->deinterlaceAutoEnabled = !rto->deinterlaceAutoEnabled;
                    if (rto->deinterlaceAutoEnabled) {
                        ; // SerialMprintln("on");
                    } else {
                        ; // SerialMprintln("off");
                    }
                    break;
                case 'p':
                    if (!Tv5725::Deinterlacer::motionAdaptEngaged()) {
                        disableScanlines();
                        enableMotionAdaptDeinterlace();
                    } else {
                        disableMotionAdaptDeinterlace();
                    }
                    break;
                case 'k':
                    if (!bypassCanBeDisplayed()) {
                        debugPrintf("bypass refused: source line rate too low\n");
                        break;
                    }
                    Tv5725::RgbhvOutput::chooseBypass();
                    enterHdBypass();
                    break;
                case 'K':
                    if (!bypassCanBeDisplayed()) {
                        debugPrintf("pass refused: source line rate too low to bypass\n");
                        break;
                    }
                    // The RESOLUTION is not touched. Handing the source over is a
                    // different fact about the same output, so it is stored on
                    // its own and leaving returns to the resolution the user
                    // chose. docs/video-source-acquisition.md
                    //
                    // A TOGGLE, and the engine switches the route both ways: the
                    // preference is the permission, and the pass that follows
                    // acts on it. Entering from here as well left the row with
                    // no way to say OFF.
                    uopt->preferScalingRgbhv = uopt->preferScalingRgbhv ? 0 : 1;
                    applyPassThroughPreference();
                    saveUserPrefs();
                    break;
                case 'T':; // SerialMprint(F("auto gain "));
                    if (uopt->enableAutoGain == 0) {
                        uopt->enableAutoGain = 1;

                        // Turning it ON restarts the search, where a preset
                        // load keeps whatever the loop settled on.
                        Tv5725::Adc::forgetGain();
                        Tv5725::Adc::armGainMeasurement(true);
                    } else {
                        uopt->enableAutoGain = 0;
                        Tv5725::Adc::armGainMeasurement(false);
                    }
                    saveUserPrefs();
                    break;
                case '!':
                    debugPrintf("sfr: %.4f pll: %lu\n",
                                Tv5725::TestBusRateMeasurement::sourceFieldRateHz(true),
                                (unsigned long)Tv5725::TestBusRateMeasurement::pllRateHz());
                    break;
                case '$': {

                    uint16_t writeAddr = 0x54;
                    const uint8_t eepromAddr = 0x50;
                    for (; writeAddr < 0x56; writeAddr++) {
                        Wire.beginTransmission(eepromAddr);
                        Wire.write(writeAddr >> 8);
                        Wire.write((uint8_t)writeAddr);
                        Wire.write(0x10);
                        Wire.endTransmission();
                        delay(5);
                    }
                    Serial.println("done");
                } break;
                case 'j':
                    Tv5725::Adc::latch();
                    break;
                case 'J':
                    Tv5725::Adc::restartPll();
                    Tv5725::SyncProcessor::forgetPositions();
                    break;
                case 'v':
                    Tv5725::Adc::choosePhaseSyncProcessor(
                        (uint8_t)((Tv5725::Adc::phaseSyncProcessor() + 1)
                                  & Tv5725::Adc::PhaseMax));
                    ; // SerialMprint("SP: ");
                    setAndLatchPhaseSP();
                    break;
                case 'b':
                    advancePhase();
                    Tv5725::Adc::latch();
                    ; // SerialMprint("ADC: ");
                    break;
                case '#':
                    applyPresets();
                    break;
                case 'n': {
                    uint16_t pll_divider = GBS::PLLAD_MD::read() + 1;

                    // Through the checked write: it latches, confirms the value
                    // took, and puts the old one back if the source stops
                    // locking. The IF registers only follow a divider that
                    // survived, so they move after the check rather than
                    // before it.
                    if (writePllAdMdChecked(pll_divider)) {
                        GBS::IF_HSYNC_RST::write((pll_divider / 2));
                        // Both ends of this window come from the engine's own
                        // start, rather than a second copy of the number or a
                        // read of the chip.
                        GBS::IF_LINE_ST::write(Tv5725::CaptureWindow::ProgressiveStart);
                        GBS::IF_LINE_SP::write(Tv5725::CaptureWindow::ProgressiveStart
                            + ((pll_divider / 2) + 1));
                        inputAcquisition.placeClampWindow();
                        inputAcquisition.placeCoastWindow(0);
                    } else {
                        debugPrintf("PLLAD_MD %u refused, left at %u\n",
                            pll_divider, GBS::PLLAD_MD::read());
                    }
                } break;
                case 'Q': {
                    // Deliberately break sync, to prove the unit comes back.
                    //
                    // A divider a long way from the source's line rate cannot
                    // be sampled at, so this is the failure the checked write
                    // exists for: it should restore the previous value, report,
                    // and leave the unit reachable throughout. Nothing else
                    // reaches this path on demand, which is why it is here
                    // rather than in a test harness.
                    uint16_t current = GBS::PLLAD_MD::read();
                    uint16_t hostile = (current > 2048) ? (current / 2) : (current * 2);
                    debugPrintf("PLLAD_MD: deliberately trying %u (from %u)\n", hostile, current);
                    bool ok = writePllAdMdChecked(hostile);
                    debugPrintf("PLLAD_MD: %s, now %u\n",
                        ok ? "accepted" : "refused and restored", GBS::PLLAD_MD::read());
                } break;
                case 'N': {
                    if (Tv5725::Deinterlacer::scanlinesApplied()) {
                        disableScanlines();
                    } else {
                        enableScanlines();
                    }
                } break;
                case 'M': {
                } break;
                case 'm':; // SerialMprint(F("syncwatcher "));
                    if (rto->syncWatcherEnabled == true) {
                        rto->syncWatcherEnabled = false;
                        Tv5725::FrameBuffer::releaseCapture();; // SerialMprintln("off");
                    } else {
                        rto->syncWatcherEnabled = true;
                        ; // SerialMprintln("on");
                    }
                    break;
                case ',':
                    printVideoTimings();
                    break;
                case 'i':
                    rto->printInfos = !rto->printInfos;
                    debugPrintf("printInfo %s\n", rto->printInfos ? "on" : "off");
                    break;
                case 'c':; // SerialMprintln(F("OTA Updates on"));
                    initUpdateOTA();
                    rto->allowUpdatesOTA = true;
                    break;
                case 'G':; // SerialMprint(F("Debug Pings "));
                    if (!rto->enableDebugPings) {
                        ; // SerialMprintln("on");
                        rto->enableDebugPings = 1;
                    } else {
                        ; // SerialMprintln("off");
                        rto->enableDebugPings = 0;
                    }
                    break;
                case 'u':
                    Tv5725::MemoryBus::restart();
                    break;
                case 'f':; // SerialMprint(F("peaking "));
                    if (uopt->wantPeaking == 0) {
                        uopt->wantPeaking = 1;
                        Tv5725::VideoProcessor::setPeaking(true);
                    } else if (!uopt->wantSharpness) {
                        uopt->wantPeaking = 0;
                        Tv5725::VideoProcessor::setPeaking(false);
                    }
                    saveUserPrefs();
                    break;
                case 'F':; // SerialMprint(F("ADC filter "));
                    if (GBS::ADC_FLTR::read() > 0) {
                        GBS::ADC_FLTR::write(0); // ADC Internal Filter Control  0:150M 1: 110M 2:70M 3:40M
                        ;                        // SerialMprintln("off");
                    } else {
                        GBS::ADC_FLTR::write(3);
                        ; // SerialMprintln("on");
                    }
                    break;
                case 'L': {

                    // uopt->wantOutputComponent = !uopt->wantOutputComponent;
                    // // OutputComponentOrVGA();
                    // saveUserPrefs();

                    // PresetPreference backup = uopt->presetPreference;
                    // uopt->presetPreference = Output720P;
                    // applyPresets();
                    // uopt->presetPreference = backup;
                } break;
                case 'l':;
                    Tv5725::SyncProcessor::reset();
                    break;
                case 'W':
                    toggleFrameTimeLock(false);
                    break;
                case '0':
                    moveHS(4, true);
                    break;
                case '1':
                    moveHS(4, false);
                    break;
                case '3':
                    //
                    break;
                // Vertical zoom. The scale is computed, not set, so there is
                // nothing to clamp.
                //
                // Carries its own magnitude like the four pads: without it the
                // only step is ControlSteps::Zoom, which stepUnits() scales by
                // the magnification, so which capture widths are reachable
                // depends on the OUTPUT resolution.
                case '4':
                    geometryControls.verticalZoom(-pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    break;
                case '5':
                    geometryControls.verticalZoom(+pressStep(pressPixels, Tv5725::ControlSteps::Zoom));
                    break;
                // Move: one path for every source, the same geometryPan
                // the pads use. docs/firmware-geometry-engine.md
                case '6':
                    geometryControls.horizontalPan(-pressStep(pressPixels, Tv5725::ControlSteps::Pan));
                    break;
                case '7':
                    geometryControls.horizontalPan(+pressStep(pressPixels, Tv5725::ControlSteps::Pan));
                    break;
                case '8':

                    invertHS();
                    invertVS();

                    break;
                case 'o': {
                    const uint8_t inForce = Tv5725::Adc::oversampleInForce();
                    const uint8_t wanted = inForce == 1 ? 2 : (inForce == 2 ? 4 : 1);
                    Tv5725::Adc::applyOversample(GBS::PLLAD_KS::read(), wanted);
                    Tv5725::Adc::latch();
                    delay(4);
                    inputAcquisition.acquireSamplingPhase();
                    ; // SerialMprint("OSR ");
                    ; // SerialMprintln("x");
                    Tv5725::Adc::forgetPhase();
                } break;
                case 'g':
                    inputStage++;

                    if (inputStage > 0) {
                        if (inputStage == 1) {
                            segmentCurrent = Serial.parseInt();
                            ; // SerialMprint("G");
                            ; // SerialMprint(segmentCurrent);
                        } else if (inputStage == 2) {
                            char szNumbers[3];
                            szNumbers[0] = Serial.read();
                            szNumbers[1] = Serial.read();
                            szNumbers[2] = '\0';

                            registerCurrent = strtol(szNumbers, NULL, 16);
                            ; // SerialMprint("R");
                            ; // SerialMprint(registerCurrent, HEX);
                            if (segmentCurrent <= 5) {
                                writeOneByte(0xF0, segmentCurrent);
                                readFromRegister(registerCurrent, 1, &readout);
                                ; // SerialMprint(" value: 0x");
                                ; // SerialMprintln(readout, HEX);
                            } else {
                                discardSerialRxData();
                                ; // SerialMprintln("abort");
                            }
                            inputStage = 0;
                        }
                    }
                    break;
                case 's':
                    inputStage++;

                    if (inputStage > 0) {
                        if (inputStage == 1) {
                            segmentCurrent = Serial.parseInt();
                            ; // SerialMprint("S");
                            ; // SerialMprint(segmentCurrent);
                        } else if (inputStage == 2) {
                            char szNumbers[3];
                            for (uint8_t a = 0; a <= 1; a++) {

                                if ((Serial.peek() >= '0' && Serial.peek() <= '9') ||
                                    (Serial.peek() >= 'a' && Serial.peek() <= 'f') ||
                                    (Serial.peek() >= 'A' && Serial.peek() <= 'F')) {
                                    szNumbers[a] = Serial.read();
                                } else {
                                    szNumbers[a] = 0;
                                    Serial.read();
                                }
                            }
                            szNumbers[2] = '\0';

                            registerCurrent = strtol(szNumbers, NULL, 16);
                            ; // SerialMprint("R");
                            ; // SerialMprint(registerCurrent, HEX);
                        } else if (inputStage == 3) {
                            char szNumbers[3];
                            for (uint8_t a = 0; a <= 1; a++) {
                                if ((Serial.peek() >= '0' && Serial.peek() <= '9') ||
                                    (Serial.peek() >= 'a' && Serial.peek() <= 'f') ||
                                    (Serial.peek() >= 'A' && Serial.peek() <= 'F')) {
                                    szNumbers[a] = Serial.read();
                                } else {
                                    szNumbers[a] = 0;
                                    Serial.read();
                                }
                            }
                            szNumbers[2] = '\0';

                            inputToogleBit = strtol(szNumbers, NULL, 16);
                            if (segmentCurrent <= 5) {
                                writeOneByte(0xF0, segmentCurrent);
                                readFromRegister(registerCurrent, 1, &readout);
                                ; // SerialMprint(" (was 0x");
                                ; // SerialMprint(readout, HEX);
                                ; // SerialMprint(")");
                                writeOneByte(registerCurrent, inputToogleBit);
                                readFromRegister(registerCurrent, 1, &readout);
                                ; // SerialMprint(" is now: 0x");
                                ; // SerialMprintln(readout, HEX);
                            } else {
                                discardSerialRxData();
                                ; // SerialMprintln("abort");
                            }
                            inputStage = 0;
                        }
                    }
                    break;
                case 't':
                    inputStage++;

                    if (inputStage > 0) {
                        if (inputStage == 1) {
                            segmentCurrent = Serial.parseInt();
                            ; // SerialMprint("T");
                            ; // SerialMprint(segmentCurrent);
                        } else if (inputStage == 2) {
                            char szNumbers[3];
                            for (uint8_t a = 0; a <= 1; a++) {

                                if ((Serial.peek() >= '0' && Serial.peek() <= '9') ||
                                    (Serial.peek() >= 'a' && Serial.peek() <= 'f') ||
                                    (Serial.peek() >= 'A' && Serial.peek() <= 'F')) {
                                    szNumbers[a] = Serial.read();
                                } else {
                                    szNumbers[a] = 0;
                                    Serial.read();
                                }
                            }
                            szNumbers[2] = '\0';

                            registerCurrent = strtol(szNumbers, NULL, 16);
                            ; // SerialMprint("R");
                            ; // SerialMprint(registerCurrent, HEX);
                        } else if (inputStage == 3) {
                            if (Serial.peek() >= '0' && Serial.peek() <= '7') {
                                inputToogleBit = Serial.parseInt();
                            } else {
                                inputToogleBit = 255;
                            }; // SerialMprint(" Bit: ");
                            ; // SerialMprint(inputToogleBit);
                            inputStage = 0;
                            if ((segmentCurrent <= 5) && (inputToogleBit <= 7)) {
                                writeOneByte(0xF0, segmentCurrent);
                                readFromRegister(registerCurrent, 1, &readout);
                                ; // SerialMprint(" (was 0x");
                                ; // SerialMprint(readout, HEX);
                                ; // SerialMprint(")");
                                writeOneByte(registerCurrent, readout ^ (1 << inputToogleBit));
                                readFromRegister(registerCurrent, 1, &readout);
                                ; // SerialMprint(" is now: 0x");
                                ; // SerialMprintln(readout, HEX);
                            } else {
                                discardSerialRxData();
                                inputToogleBit = registerCurrent = 0;
                                ; // SerialMprintln("abort");
                            }
                        }
                    }
                    break;
                case '<': {
                    if (segmentCurrent != 255 && registerCurrent != 255) {
                        writeOneByte(0xF0, segmentCurrent);
                        readFromRegister(registerCurrent, 1, &readout);
                        writeOneByte(registerCurrent, readout - 1);
                        Serial.print("S");
                        Serial.print(segmentCurrent);
                        Serial.print("_");
                        Serial.print(registerCurrent, HEX);
                        readFromRegister(registerCurrent, 1, &readout);
                        Serial.print(" : ");
                        Serial.println(readout, HEX);
                    }
                } break;
                case '>': {
                    if (segmentCurrent != 255 && registerCurrent != 255) {
                        writeOneByte(0xF0, segmentCurrent);
                        readFromRegister(registerCurrent, 1, &readout);
                        writeOneByte(registerCurrent, readout + 1);
                        Serial.print("S");
                        Serial.print(segmentCurrent);
                        Serial.print("_");
                        Serial.print(registerCurrent, HEX);
                        readFromRegister(registerCurrent, 1, &readout);
                        Serial.print(" : ");
                        Serial.println(readout, HEX);
                    }
                } break;
                case '_': {
                    const Tv5725::TestBus::Hold held;
                    Tv5725::TestBus::selectInputVsync();
                    Serial.println(debugPinPulseTicks());
                } break;
                case '~':
                    goLowPowerWithInputDetection();
                    break;
                case 'w': {

                    uint16_t value = 0;
                    String what = Serial.readStringUntil(' ');

                    if (what.length() > 5) {
                        ; // SerialMprintln(F("abort"));
                        inputStage = 0;
                        break;
                    }
                    if (what.equals("f")) {
                        if (rto->displayClock.driving()) {
                            Serial.print(F("old freqExtClockGen: "));
                            Serial.println((uint32_t)rto->displayClock.hzNow());
                            rto->displayClock.assumeHz(Serial.parseInt());

                            uint32_t wanted = rto->displayClock.hzNow();
                            if (wanted >= 1000000 && wanted <= 250000000) {
                                clockGen.setFrequency(wanted);
                                Tv5725::SyncProcessor::forgetPositions();
                            }
                            Serial.print(F("set freqExtClockGen: "));
                            Serial.println((uint32_t)rto->displayClock.hzNow());
                        }
                        break;
                    }

                    value = Serial.parseInt();
                    if (value < 4096) {
                        ; // SerialMprint("set ");
                        ; // SerialMprint(what);
                        ; // SerialMprint(" ");
                        ; // SerialMprintln(value);
                        if (what.equals("sog")) {
                            Tv5725::SyncOnGreen::choose(value);
                            Tv5725::SyncOnGreen::putInForce();
                        } else if (what.equals("ifini")) {
                            inputFormatter.writeLineCounterStart(value);
                        } else if (what.equals("vsstc")) {
                            Tv5725::SyncProcessor::writeSdVsyncStart(value);
                        } else if (what.equals("vsspc")) {
                            Tv5725::SyncProcessor::writeSdVsyncStop(value);
                        }
                    } else {
                        ; // SerialMprintln("abort");
                    }
                } break;
                case 'x': {
                    uint16_t if_hblank_scale_stop = GBS::IF_HBIN_SP::read();
                    GBS::IF_HBIN_SP::write(if_hblank_scale_stop + 1);
                    ; // SerialMprint("1_26: ");
                    ; // SerialMprintln((if_hblank_scale_stop + 1), HEX);
                } break;
                case 'X': {
                    uint16_t if_hblank_scale_stop = GBS::IF_HBIN_SP::read();
                    GBS::IF_HBIN_SP::write(if_hblank_scale_stop - 1);
                    ; // SerialMprint("1_26: ");
                    ; // SerialMprintln((if_hblank_scale_stop - 1), HEX);
                } break;
                case 'V': {
                    ; // SerialMprint(F("step response "));
                    uopt->wantStepResponse = !uopt->wantStepResponse;
                    geometry.applyOutputPictureFilters(uopt->wantSharpness,
                                                       uopt->wantStepResponse);
                    saveUserPrefs();
                } break;
                case ':':
                    frameSync.matchRate(sourceSampling.settledFieldRateHz());
                    break;
                case ';':
                    externalClockGenResetClock();
                    if (rto->displayClock.driving()) {
                        rto->displayClock.detach();
                        Serial.println(F("ext clock gen bypass"));
                    } else {
                        rto->displayClock.driveWith(clockGen);
                        Serial.println(F("ext clock gen active"));
                        frameSync.matchRate(sourceSampling.settledFieldRateHz());
                    }
                    //{

                    //}
                    break;
                default:
                    Serial.print(F("unknown command "));
                    Serial.println(serialCommand, HEX);
                    break;
            }

            delay(1);

            frameSync.defer(millis());

            if (!Serial.available()) {

                if (serialCommand != 'D') {
                    serialCommand = '@';
                }
                handleWiFi(1);
            }
        }

#if GBS_DEBUG
        if (pendingAvFrame >= 0) {
            const uint8_t frame = (uint8_t)pendingAvFrame;
            pendingAvFrame = -1;
            sendInputFrame(frame);
            char line[48];
            snprintf_P(line, sizeof(line), PSTR("av frame: 0x%02x sent"),
                       (unsigned)frame);
            tv5725Log(line);
        }

        if (pendingTestBusSweep) {
            pendingTestBusSweep = false;
            sweepTestBus(pendingTestBusMs, pendingTestBusSp, pendingTestBusSig,
                         pendingTestBusIf);
        }
        if (pendingCoast) {
            pendingCoast = false;
            applyCoastOverride(pendingCoastApply, pendingCoastClear,
                pendingCoastPre, pendingCoastPost);
        }
        if (pendingSampleClock) {
            pendingSampleClock = false;
            applySampleClock(pendingSampleClockApply,
                             pendingSampleClockDivider,
                             pendingSampleClockOversample);
        }
        if (pendingDividerHold) {
            pendingDividerHold = false;
            holdSampleClock(pendingHeldDivider);
        }
        if (pendingInputScale) {
            pendingInputScale = false;
            const Tv5725::InputScale wanted(pendingInputScaleRate);
            debugPrintf("input scale: %u %s\n", (unsigned)wanted.increment(),
                        geometry.setInputScale(wanted) ? "applied" : "refused");
        }
        if (pendingFullFramingChange) {
            pendingFullFramingChange = false;
            geometry.forceFullFraming(pendingFullFraming);
            inputAcquisition.resolveFromSource();
            debugPrintf("framing: full %s\n", pendingFullFraming ? "on" : "off");
        }
        if (pendingRestart) {
            pendingRestart = false;
            debugPrintf("restart: asked for over HTTP\n");
            delay(50);            // let the reply leave before the stack goes
            ESP.restart();
        }
#endif
#if GBS_TRACE_WRITES
        if (pendingWriteReplay) {
            pendingWriteReplay = false;
            Tv5725::WriteTrace::replay(pendingWriteReplayFirst,
                                       pendingWriteReplayLast,
                                       pendingWriteReplayGaps);
        }
#endif
#if GBS_SAMPLING_LOG
        if (pendingSamplingRates) {
            pendingSamplingRates = false;
            samplingLog.rates(millis(), pendingSamplingA);
        }
        if (pendingSamplingMonitor) {
            pendingSamplingMonitor = false;
            samplingLog.monitor(millis(), pendingSamplingA, pendingSamplingD);
        }
        if (pendingSamplingSweep) {
            pendingSamplingSweep = false;
            samplingLog.sweep(millis(), pendingSamplingA, pendingSamplingB,
                              pendingSamplingC, (uint16_t)pendingSamplingD,
                              Tv5725::Adc::oversampleInForce(),
                              inputAcquisition.sourceLineRateHz());
        }
#endif
        if (pendingTuneSteps != 0) {
            const int16_t steps = pendingTuneSteps;
            pendingTuneSteps = 0;
            applyTune(pendingTune, steps);
        }

        if (pendingNudgeSteps != 0) {
            const int16_t steps = pendingNudgeSteps;
            pendingNudgeSteps = 0;
            geometryControls.nudge(pendingNudge, steps);
        }

        if (pendingSlotSelection != 0) {
            const uint8_t wanted = pendingSlotSelection;
            pendingSlotSelection = 0;

            uopt->presetSlot = wanted;
            saveUserPrefs();
            applySelectedSlot();
        }

        if (pendingOutputMode != NULL) {
            const Tv5725::OutputMode *const wanted = pendingOutputMode;
            pendingOutputMode = NULL;

            // A RESOLUTION IS A COMMAND TO SCALE: pass-through holds a source
            // off the resolution just chosen, so choosing one leaves it, and
            // before the change or the next pass routes the source back.
            chooseOutputMode(wanted);
            uopt->preferScalingRgbhv = 1;
            applyPassThroughPreference();
            changeOutputResolution();
            saveUserPrefs();
        }

        if (pendingInputSelection != VideoSourceSelection::None) {
            // Cleared before acting, not after: every handler below blocks for
            // seconds while detection runs, and a second request landing in that
            // window must queue a new selection rather than be swallowed.
            const VideoSourceSelection::Id wanted = (VideoSourceSelection::Id)pendingInputSelection;
            pendingInputSelection = VideoSourceSelection::None;

            switch (wanted) {
                case VideoSourceSelection::Rgbs: InputRGBs(); break;
                case VideoSourceSelection::RgsB: InputRGsB(); break;
                case VideoSourceSelection::Vga: InputVGA(); break;
                case VideoSourceSelection::Ypbpr: InputYUV(); break;
                case VideoSourceSelection::SVideo: InputSV(); break;
                case VideoSourceSelection::Composite: InputAV(); break;
                default: break;
            }
        }

        if (userCommand != '@') {
            // printf("Web %c \n", userCommand);

            handleType2Command(userCommand);
            userCommand = '@';
            frameSync.defer(millis());
            handleWiFi(1);

            // printf("uopt->presetSlot %d  \n", uopt->presetSlot);
        }
        Tim_web = millis();
    }
}

#if defined(ESP8266)
#include "webui_html.h"

// Fill, 4:3, 16:9, 5:4 and round. A preset cycle rather than a number, because
// the remote has one button for it and the four are what any source here wants;
// the shape is stored against the source, so the cycle starts from whatever that
// source was left at. docs/aspect-ratio.md
static void cycleAspect()
{
    const Tv5725::Aspect wanted = geometry.aspect().next();
    const bool moved = geometry.setAspect(wanted);

    char line[48];
    snprintf_P(line, sizeof(line), PSTR("aspect: %u%s"),
               (unsigned)wanted.tenThousandths(), moved ? "" : " (refused)");
    debugPrintf("%s\n", line);
}

void handleType2Command(char argument)
{
    switch (argument) {
        case '0':

            if (uopt->PalForce60 == 0) {
                uopt->PalForce60 = 1;
            } else {
                uopt->PalForce60 = 0;
            }
            saveUserPrefs();

            break;
        case '1':
            // reset to defaults button
            webSocket.close();
            loadDefaultUserOptions();
            saveUserPrefs();
            Serial.println(F("options set to defaults, restarting"));
            delay(60);
            ESP.reset(); // don't use restart(), messes up websocket reconnects
            //
            break;
        case '2':
            //
            break;
        // A slot holds the framing the user tuned, against the source it was
        // tuned for -- the inputs to the calculation, never the registers it
        // produced. docs/framing-presets.md
        //
        // Both say what they did: a silent no-op leaves someone pressing save
        // and believing it worked.
        case '3': // load the selected slot
            SerialM.println(applySelectedSlot()
                ? F("slot load: framing restored")
                : F("slot load: nothing stored for this source"));
            break;
        case '4': // save custom preset
            SerialM.println(storeSlotFraming(currentSlotIndex())
                ? F("slot save: framing stored")
                : F("slot save: no source measured, or the table is full"));
            break;
        case '5':
            toggleFrameTimeLock(true);
            break;
        case '6':
            //
            break;
        case '7':
            uopt->wantScanlines = !uopt->wantScanlines;
            //  SerialMprint(F("scanlines: "));
            if (uopt->wantScanlines) {
                //  SerialMprintln(F("on (Line Filter recommended)"));
            } else {
                disableScanlines();
                // SerialMprintln("off");
            }
            saveUserPrefs();
            break;
        case '9':
            //
            break;
        case 'a':
            webSocket.close();
            Serial.println(F("restart"));
            delay(60);
            ESP.reset(); // don't use restart(), messes up websocket reconnects
            break;
        case 'e': // print files on the filesystem
        {
            Dir dir = LittleFS.openDir("/");
            while (dir.next()) {
                ;         // SerialMprint(dir.fileName());
                ;         // SerialMprint(" ");
                ;         // SerialMprintln(dir.fileSize());
                delay(1); // wifi stack
            }
            ////
        } break;
        case 'f':
        case 'g':
        case 'h':
        case 'j':
        case 'p':
        case 's':
        case 'L': {
            if (argument == 'f')
                chooseOutputMode(&Tv5725::Mode960p);
            if (argument == 'g')
                chooseOutputMode(&Tv5725::Mode720p);
            if (argument == 'h')
                chooseOutputMode(&Tv5725::Mode480p);
            if (argument == 'j')
                chooseOutputMode(&Tv5725::Mode576p);
            if (argument == 'p')
                chooseOutputMode(&Tv5725::Mode1024p);
            if (argument == 's')
                chooseOutputMode(&Tv5725::Mode1080p);

            // A RESOLUTION IS A COMMAND TO SCALE. Pass-through is the one thing
            // that can hold a source off the resolution just chosen -- it hands
            // the source's own timing to the encoder and the engine's raster
            // reaches nothing -- so choosing one leaves it. Before the change,
            // or the next pass routes the source straight back.
            uopt->preferScalingRgbhv = 1;
            applyPassThroughPreference();

            changeOutputResolution();
            saveUserPrefs();
        } break;
        case 'i':
            // toggle active frametime lock method
            if (!rto->displayClock.driving()) {
                frameSync.reset(uopt->frameTimeLockMethod);
            }
            if (uopt->frameTimeLockMethod == 0) {
                uopt->frameTimeLockMethod = 1;
            } else if (uopt->frameTimeLockMethod == 1) {
                uopt->frameTimeLockMethod = 0;
            }
            saveUserPrefs();
            break;
        case 'l':
            // cycle through available SDRAM clocks
            {
                uint8_t PLL_MS = GBS::PLL_MS::read();
                uint8_t memClock = 0;

                if (PLL_MS == 0)
                    PLL_MS = 2;
                else if (PLL_MS == 2)
                    PLL_MS = 7;
                else if (PLL_MS == 7)
                    PLL_MS = 4;
                else if (PLL_MS == 4)
                    PLL_MS = 3;
                else if (PLL_MS == 3)
                    PLL_MS = 5;
                else if (PLL_MS == 5)
                    PLL_MS = 0;

                switch (PLL_MS) {
                    case 0:
                        memClock = 108;
                        break;
                    case 1:
                        memClock = 81;
                        break; // goes well with 4_2C = 0x14, 4_2D = 0x27
                    case 2:
                        memClock = 10;
                        break; // feedback clock
                    case 3:
                        memClock = 162;
                        break;
                    case 4:
                        memClock = 144;
                        break;
                    case 5:
                        memClock = 185;
                        break; // slight OC
                    case 6:
                        memClock = 216;
                        break; // !OC!
                    case 7:
                        memClock = 129;
                        break;
                    default:
                        break;
                }
                GBS::PLL_MS::write(PLL_MS);
                Tv5725::MemoryBus::restart();
                if (memClock != 10) {
                    ; // SerialMprint(F("SDRAM clock: "));
                    ; // SerialMprint(memClock);
                    ; // SerialMprintln("Mhz");
                } else {
                    ; // SerialMprint(F("SDRAM clock: "));
                    ; // SerialMprintln(F("Feedback clock"));
                }
            }
            break;
        case 'G':
            cycleAspect();
            break;
        case 'm':; // SerialMprint(F("Line Filter: "));
            uopt->wantVdsLineFilter = !uopt->wantVdsLineFilter;
            Tv5725::VideoProcessor::setLineFilter(uopt->wantVdsLineFilter);
            saveUserPrefs();
            break;
        case 'n':; // SerialMprint(F("ADC gain++ : "));
            uopt->enableAutoGain = 0;
            Tv5725::Adc::stepGain(-1);
            ; // SerialMprintln(GBS::ADC_RGCTRL::read(), HEX);
            break;
        case 'o':; // SerialMprint(F("ADC gain-- : "));
            uopt->enableAutoGain = 0;
            Tv5725::Adc::stepGain(+1);
            ; // SerialMprintln(GBS::ADC_RGCTRL::read(), HEX);
            break;
        // 'A'-'D' were the border mask, removed: the engine computes the
        // display window to hug the picture, so a manual mask can only
        // disagree with it. docs/firmware-geometry-engine.md
        case 'q':
            if (uopt->deintMode != 1) {
                uopt->deintMode = 1;
                disableMotionAdaptDeinterlace();
                disableScanlines();
                saveUserPrefs();
            }; // SerialMprintln(F("Deinterlacer: Bob"));
            break;
        case 'r':
            if (uopt->deintMode != 0) {
                uopt->deintMode = 0;
                saveUserPrefs();
                // will enable next loo p()
            }; // SerialMprintln(F("Deinterlacer: Motion Adaptive"));
            break;
        case 't':
            // unused now
            ; // SerialMprint(F("6-tap: "));
            if (uopt->wantTap6 == 0) {
                uopt->wantTap6 = 1;
                Tv5725::VideoProcessor::setSixTapFilter(true);
                Tv5725::Deinterlacer::applySixTapFilter(true);
                ; // SerialMprintln("on");
            } else {
                uopt->wantTap6 = 0;
                Tv5725::VideoProcessor::setSixTapFilter(false);
                Tv5725::Deinterlacer::applySixTapFilter(false);
                ; // SerialMprintln("off");
            }
            saveUserPrefs();
            break;
        case 'u':
            // restart to attempt wifi station mode connect
            printf("RestWiFi.Sta : %d\n", WIFI_STA);
            delay(30);
            WiFi.mode(WIFI_STA);
            WiFi.hostname(device_hostname_partial); // _full
            delay(30);
            ESP.reset();
            break;
        case 'w':
            uopt->enableCalibrationADC = !uopt->enableCalibrationADC;
            saveUserPrefs();
            break;
        case 'x':
            uopt->preferScalingRgbhv = !uopt->preferScalingRgbhv;
            applyPassThroughPreference();
            saveUserPrefs();
            // Applied to the source in force rather than at the next one. The
            // preference is the user's veto on the route, and a bypass
            // reference has to be reachable while a source stands still.
            inputAcquisition.resolveFromSource();
            break;
        case 'X':; // SerialMprint(F("ExternalClockGenerator "));
            if (uopt->disableExternalClockGenerator == 0) {
                uopt->disableExternalClockGenerator = 1;
                // printf("disabled\n");
            } else {
                uopt->disableExternalClockGenerator = 0;
                // printf("enabled\n");
            }
            saveUserPrefs();
            break;
        case 'z':
            // sog sync separator level
            if (Tv5725::SyncOnGreen::level() > 0) {
                Tv5725::SyncOnGreen::choose(Tv5725::SyncOnGreen::level() - 1);
            } else {
                Tv5725::SyncOnGreen::choose(16);
            }
            Tv5725::SyncOnGreen::putInForce();
            inputAcquisition.acquireSamplingPhase();
            ; // SerialMprint("Phase: ");
            ; // SerialMprint(" SOG: ");
            ; // SerialMprint(Tv5725::SyncOnGreen::level());
            ; // SerialMprintln();
            break;
        case 'E':
            // test option for now
            ; // SerialMprint(F("IF Auto Offset: "));
            toggleIfAutoOffset();
            if (GBS::IF_AUTO_OFST_EN::read()) {
                ; // SerialMprintln("on");
            } else {
                ; // SerialMprintln("off");
            }
            break;
        case 'F':
            // freeze pic
            if (GBS::CAPTURE_ENABLE::read()) {
                GBS::CAPTURE_ENABLE::write(0);
            } else {
                GBS::CAPTURE_ENABLE::write(1);
            }
            break;
        case 'K':
            // scanline strength
            if (uopt->scanlineStrength >= 0x10) {
                uopt->scanlineStrength -= 0x10;
            } else {
                uopt->scanlineStrength = 0x50;
            }
            Tv5725::Deinterlacer::applyScanlineStrength(uopt->scanlineStrength);
            saveUserPrefs();
            break;
        case 'W':
            uopt->wantSharpness = uopt->wantSharpness ? 0 : 1;
            geometry.applyOutputPictureFilters(uopt->wantSharpness,
                                               uopt->wantStepResponse);
            saveUserPrefs();
            break;
        // The colour balance. These six used to step VDS_Y_OFST, VDS_U_OFST and
        // VDS_V_OFST a count at a time, which is the same three dimensions in
        // the basis nothing on the menu shows -- and a second owner of the
        // registers Tv5725::ColourBalance now writes. docs/osd-menu.md
        // Keeping the balance is a press of its own, as it was on the chain:
        // Left and Right are held keys, and a save per step would write flash a
        // hundred times for one adjustment. The steps themselves are not letters
        // -- a row names the value it moves. docs/osd-menu.md
        case 'Y':
            saveUserPrefs();
            break;

        // The AV module's four single presses. Send_Line, Send_Smooth and
        // Send_Compatibility each save the preferences themselves.
        case 'b':
            avo->lineDouble = !avo->lineDouble;
            Send_Line(avo->lineDouble);
            break;
        case 'c':
            // Smoothing is a property of the doubled line, so it does nothing
            // while the doubler is out -- which is the chain's gate too.
            if (avo->lineDouble) {
                avo->smooth = !avo->smooth;
                Send_Smooth(avo->smooth);
            }
            break;
        case 'd':
            avo->rgbCompatible = !avo->rgbCompatible;
            Send_Compatibility(avo->rgbCompatible);
            if (GBS::ADC_INPUT_SEL::read())
                applyPresets();
            break;
        case 'k':
            resetAvPicture();
            break;
        case 'V':
            // Цвет +
            GBS::VDS_VCOS_GAIN::write(GBS::VDS_VCOS_GAIN::read() + 1);
            GBS::VDS_UCOS_GAIN::write(GBS::VDS_UCOS_GAIN::read() + 1);
            ; // SerialMprint(F("Color + : "));
            ; // SerialMprintln(GBS::VDS_VCOS_GAIN::read(), DEC); // потолок 3D
            if (GBS::VDS_UCOS_GAIN::read() >= 0x39) {
                GBS::VDS_UCOS_GAIN::write(0x1C);
                GBS::VDS_VCOS_GAIN::write(0x29);
            }
            break;
        case 'R':
            // Цвет -
            GBS::VDS_UCOS_GAIN::write(GBS::VDS_UCOS_GAIN::read() - 1);
            GBS::VDS_VCOS_GAIN::write(GBS::VDS_VCOS_GAIN::read() - 1);
            ; // SerialMprint(F("Color - : "));
            ; // SerialMprintln(GBS::VDS_VCOS_GAIN::read(), DEC); // потолок 14
            if (GBS::VDS_UCOS_GAIN::read() <= 0x07) {
                GBS::VDS_UCOS_GAIN::write(0x1C);
            }
            if (GBS::VDS_VCOS_GAIN::read() <= 0x14) {
                GBS::VDS_VCOS_GAIN::write(0x29);
            }
            break;
        case 'O':
            // Инфо
            if (GBS::ADC_INPUT_SEL::read() == 1) {
                ; // SerialMprintln("RGB timings");
                ; // SerialMprintln(F("------------ "));
                ; // SerialMprint(F("U_OFFSET "));
                ; // SerialMprintln(GBS::VDS_U_OFST::read(), DEC);
                ; // SerialMprint(F("V_OFFSET "));
                ; // SerialMprintln(GBS::VDS_V_OFST::read(), DEC);
                ; // SerialMprint(F("Y_OFFSET "));
                ; // SerialMprintln(GBS::VDS_Y_OFST::read(), DEC);
                ; // SerialMprintln(F("------------ "));
                ; // SerialMprint(F("UCOS_GAIN "));
                ; // SerialMprintln(GBS::VDS_UCOS_GAIN::read(), DEC);
                ; // SerialMprint(F("VCOS_GAIN "));
                ; // SerialMprintln(GBS::VDS_VCOS_GAIN::read(), DEC);
                ; // SerialMprint(F("Y_GAIN "));
                ; // SerialMprintln(GBS::VDS_Y_GAIN::read(), DEC);
            } else {
                ; // SerialMprintln("YPbPr timings");
                ; // SerialMprintln(F("------------ "));
                ; // SerialMprint(F("U_OFFSET "));
                ; // SerialMprintln(GBS::VDS_U_OFST::read(), DEC);
                ; // SerialMprint(F("V_OFFSET "));
                ; // SerialMprintln(GBS::VDS_V_OFST::read(), DEC);
                ; // SerialMprint(F("Y_OFFSET "));
                ; // SerialMprintln(GBS::VDS_Y_OFST::read(), DEC);
                ; // SerialMprintln(F("------------ "));
                ; // SerialMprint(F("UCOS_GAIN "));
                ; // SerialMprintln(GBS::VDS_UCOS_GAIN::read(), DEC);
                ; // SerialMprint(F("VCOS_GAIN "));
                ; // SerialMprintln(GBS::VDS_VCOS_GAIN::read(), DEC);
                ; // SerialMprint(F("Y_GAIN "));
                ; // SerialMprintln(GBS::VDS_Y_GAIN::read(), DEC);
            }
            break;
        case 'U': // Default
            // The chroma gains and the ADC offsets are not the balance's; the
            // offsets and the luma gain are, and the rest the balance returns to
            // is the colour space's own.
            GBS::VDS_UCOS_GAIN::write(0x1C);
            GBS::VDS_VCOS_GAIN::write(0x29);
            Tv5725::Adc::applyHeldOffset();
            geometry.colour().reset();
            applyColourBalance();
            saveUserPrefs();
            break;
        case 'I':
            if (IR == 0) {
                oled_menuItem = 5;
                ; // SerialMprintln("IR remote: DEC");
            } else if (IR == 1) {
                oled_menuItem = 0;
                ; // SerialMprintln("IR remote: OFF");
                IR = 0;
            }
            break;
        default:
            break;
    }
}

WiFiEventHandler disconnectedEventHandler;

#if GBS_DEBUG
// Parse a hex query parameter into 0..limit; 0x prefix optional. Hex, not
// strtol base-0: an unprefixed r=11 would read as decimal 11 and poke 0x0b.
// False if missing, malformed or out of range.
static bool getHexParam(AsyncWebServerRequest *request, const char *name, long limit, long *out)
{
    if (!request->hasParam(name)) {
        return false;
    }

    String raw = request->getParam(name)->value();
    char *end = NULL;
    long value = strtol(raw.c_str(), &end, 16);

    if (end == raw.c_str() || *end != '\0' || value < 0 || value > limit) {
        return false;
    }

    *out = value;
    return true;
}
#endif

#if GBS_DEBUG
// The web server's register work, done in loop() so the bus has one owner.
// Reaching a TV5725 register is two I2C transactions, so an access landing
// between them uses the segment pointer the first one set.
// docs/register-bus-ownership.md
static RegisterQueue registerQueue;

// Park a request for loop() to answer, and arrange for its cancellation.
//
// onDisconnect fires from the async side, which is the context that can delete
// the request while loop() holds it, so registering the callback BEFORE
// submitting is the ordering that matters: a disconnect between the two would
// leave a job pointing at freed memory with nothing to cancel it.
static void submitRegisterJob(AsyncWebServerRequest *request, const RegisterQueue::Job &job)
{
    request->onDisconnect([request]() { registerQueue.cancel(request); });

    if (!registerQueue.submit(job)) {
        // Full. A retryable status beats a parked request nobody answers.
        request->send(503, "application/json",
            F("{\"error\":\"register queue full, retry\"}"));
    }
}

// Drains ONE job per pass, not all of them: a whole-segment read is 256 accesses
// and the rest of loop() -- the sync watcher, FrameSync, the OSD -- has to get
// its turn between them.
static void serviceRegisterQueue()
{
    RegisterQueue::Job job;
    if (!registerQueue.claim(job)) {
        return;
    }

    String body;
    switch (job.kind) {
        case RegisterQueue::ReadOne: {
            char text[64];
            snprintf_P(text, sizeof(text),
                PSTR("{\"segment\":%u,\"register\":\"0x%02x\",\"value\":\"0x%02x\"}"),
                job.segment, job.first, GBS::read(job.segment, job.first));
            body = text;
            break;
        }
        case RegisterQueue::WriteOne: {
            uint8_t was = GBS::read(job.segment, job.first);
            GBS::write(job.segment, job.first, job.value);
            uint8_t now = GBS::read(job.segment, job.first);
            char text[96];
            snprintf_P(text, sizeof(text),
                PSTR("{\"segment\":%u,\"register\":\"0x%02x\",\"was\":\"0x%02x\",\"value\":\"0x%02x\"}"),
                job.segment, job.first, was, now);
            body = text;
            break;
        }
        case RegisterQueue::ReadRange: {
            String hex;
            hex.reserve((job.last - job.first + 1) * 2 + 1);
            for (uint16_t reg = job.first; reg <= job.last; reg++) {
                char pair[3];
                snprintf_P(pair, sizeof(pair), PSTR("%02x"), GBS::read(job.segment, (uint8_t)reg));
                hex += pair;
                if ((reg & 0x0f) == 0x0f) {
                    ESP.wdtFeed();
                }
            }
            body.reserve(hex.length() + 64);
            body += F("{\"segment\":");
            body += job.segment;
            body += F(",\"from\":");
            body += job.first;
            body += F(",\"to\":");
            body += job.last;
            body += F(",\"values\":\"");
            body += hex;
            body += F("\"}");
            break;
        }
        case RegisterQueue::ReadFields: {
            const FieldRequest &request = registerQueue.fields();
            // Ten digits and a comma is the widest a 32-bit field prints.
            body.reserve(request.count() * 11 + 16);
            body += F("{\"values\":[");
            for (uint8_t i = 0; i < request.count(); i++) {
                const FieldRequest::Field &field = request.at(i);
                uint8_t bytes[FieldRequest::MaxRegisters] = {0, 0, 0, 0, 0};
                for (uint8_t b = 0; b < FieldRequest::bytesFor(field); b++) {
                    bytes[b] = GBS::read(field.segment, (uint8_t)(field.reg + b));
                }
                if (i) {
                    body += ',';
                }
                body += FieldRequest::valueFrom(field, bytes);
                if ((i & 0x07) == 0x07) {
                    ESP.wdtFeed();
                }
            }
            body += F("]}");
            break;
        }
        default:
            registerQueue.complete();
            return;
    }

    // Ask AFTER the transfer and before answering. The client may have gone
    // while we were on the bus, and the server deletes the request as soon as
    // its disconnect callback returns -- answering then writes into freed
    // memory, which is a crash that costs a trip to the bench.
    if (!registerQueue.claimCancelled()) {
        static_cast<AsyncWebServerRequest *>(job.token)->send(200, "application/json", body);
    }
    registerQueue.complete();
}
#endif

// Heap a route needs before it will answer, and a refusal that SAYS so. Nothing
// at all is what these used to send, which reads exactly like a crashed handler
// -- three OTA attempts went into one. docs/known-issues.md
//
// A class because the sketch's free functions get a prototype inserted above the
// includes, where AsyncWebServerRequest is not a type yet.
class RouteHeap {
public:
    // A route that assembles a reply: the web UI, a directory listing, a slot
    // file. One that queues a byte and answers an empty 200 costs almost
    // nothing, so its floor is only there to keep a genuinely exhausted heap
    // from being asked for a response object.
    static bool allowsAReply(AsyncWebServerRequest *request)
    {
        return allows(request, 10000);
    }

    static bool allowsAByte(AsyncWebServerRequest *request)
    {
        return allows(request, 4000);
    }

private:
    static bool allows(AsyncWebServerRequest *request, uint32_t needed)
    {
        if (ESP.getFreeHeap() > needed)
            return true;
        char body[72];
        snprintf_P(body, sizeof(body),
                   PSTR("{\"error\":\"low heap\",\"free\":%u,\"needs\":%u}"),
                   (unsigned)ESP.getFreeHeap(), (unsigned)needed);
        request->send(503, "application/json", body);
        return false;
    }
};

// A slots.bin whose length does not match SlotMetaArray is REPLACED rather than
// read. Every reader here pulls sizeof(SlotMetaArray) bytes without checking,
// so a file from a build with a different SlotMeta gives garbled names -- and
// the web UI refuses one of the wrong length and retries for ever, which reads
// as a unit that never finishes loading.
static void ensureSlotsFile()
{
    File existing = LittleFS.open(SLOTS_FILE, "r");
    const bool usable = existing && existing.size() == sizeof(SlotMetaArray);
    if (existing)
        existing.close();
    if (usable)
        return;

    SlotMetaArray slotsObject;
    for (int i = 0; i < SLOTS_TOTAL; i++) {
        slotsObject.slot[i].slot = i;
        slotsObject.slot[i].scanlines = 0;
        slotsObject.slot[i].scanlinesStrength = 0;
        slotsObject.slot[i].wantVdsLineFilter = false;
        slotsObject.slot[i].wantStepResponse = true;
        slotsObject.slot[i].wantPeaking = true;
        strncpy(slotsObject.slot[i].name, EMPTY_SLOT_NAME, 25);
    }

    File fresh = LittleFS.open(SLOTS_FILE, "w");
    if (!fresh)
        return;
    fresh.write((byte *)&slotsObject, sizeof(slotsObject));
    fresh.close();
}

void startWebserver()
{

    persWM.setApCredentials(final_ssid, ap_password);
    persWM.onConnect([]() {
    if (MDNS.begin(device_hostname_partial, WiFi.localIP()))
    { 
      
      MDNS.addService("http", "tcp", 80); 
      MDNS.announce();
    } });
    persWM.onAp([]() { ; });

    disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
    Serial.print("Station disconnected, reason: ");
    Serial.println(event.reason); });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (RouteHeap::allowsAReply(request))
    {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", webui_html, webui_html_len);
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
    } });

    server.on("/sc", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (RouteHeap::allowsAByte(request))
    {
      int params = request->params();
      
      if (params > 0)
      {
        AsyncWebParameter *p = request->getParam(0);
        
        serialCommand = p->name().charAt(0);
        serialCommandPixels = (int16_t)p->value().toInt();

        
        if (serialCommand == ' ')
        {
          serialCommand = '+';
        }
      }
      request->send(200); 
    } });

    server.on("/uc", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (RouteHeap::allowsAByte(request))
    {
      int params = request->params();
      
      if (params > 0)
      {
        AsyncWebParameter *p = request->getParam(0);
        
        userCommand = p->name().charAt(0);
      }
      request->send(200);
    } });

#if GBS_DEBUG
    // Read and write TV5725 registers over HTTP. Hex throughout, 0x optional;
    // /setreg reports the previous value, so a poke can be undone.
    // docs/gbs-control-debug-interface.md.

    // Read a whole segment in one request. /getreg costs an HTTP round trip per
    // register, so reading a segment to inspect it meant hundreds of them —
    // slow enough that tooling avoided doing it at all. Returns the values as
    // one hex string, index 0 being register `from`.
    server.on("/getregs", HTTP_GET, [](AsyncWebServerRequest *request) {
        long segment = 0, from = 0, to = 0xff;

        if (!getHexParam(request, "s", 5, &segment)) {
            request->send(400, "application/json", F("{\"error\":\"expected s=0..5\"}"));
            return;
        }
        if (request->hasParam("from") && !getHexParam(request, "from", 0xff, &from)) {
            request->send(400, "application/json", F("{\"error\":\"bad from\"}"));
            return;
        }
        if (request->hasParam("to") && !getHexParam(request, "to", 0xff, &to)) {
            request->send(400, "application/json", F("{\"error\":\"bad to\"}"));
            return;
        }
        if (to < from) {
            request->send(400, "application/json", F("{\"error\":\"to below from\"}"));
            return;
        }

        RegisterQueue::Job job;
        job.kind = RegisterQueue::ReadRange;
        job.token = request;
        job.segment = (uint8_t)segment;
        job.first = (uint8_t)from;
        job.last = (uint8_t)to;
        submitRegisterJob(request, job);
    });

    // Many fields in ONE request, answered in the order asked. /getreg costs a
    // round trip AND a loop pass each, so a whole-state read one field at a time
    // starves the loop it is photographing. The wire form is addresses --
    // `seg.reg.offset.width`, all decimal, comma separated -- because the
    // firmware has no runtime name table; the host resolves names.
    server.on("/getfields", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("f")) {
            request->send(400, "application/json",
                F("{\"error\":\"expected f=seg.reg.offset.width,...\"}"));
            return;
        }

        // Parsed here rather than in loop(): a spec the firmware cannot read is
        // the client's mistake and deserves an immediate answer, not a queue slot.
        // On the stack, because submitFields() copies it.
        FieldRequest parsed;
        if (!parsed.parse(request->getParam("f")->value().c_str())) {
            request->send(400, "application/json",
                F("{\"error\":\"bad field spec\"}"));
            return;
        }

        if (!registerQueue.submitFields(parsed, request)) {
            request->send(503, "application/json",
                F("{\"error\":\"register queue busy, retry\"}"));
        }
    });

    server.on("/getreg", HTTP_GET, [](AsyncWebServerRequest *request) {
        long segment = 0, reg = 0;

        if (!getHexParam(request, "s", 5, &segment) || !getHexParam(request, "r", 0xff, &reg)) {
            request->send(400, "application/json", F("{\"error\":\"expected s=0..5 and r=0..0xff\"}"));
            return;
        }

        RegisterQueue::Job job;
        job.kind = RegisterQueue::ReadOne;
        job.token = request;
        job.segment = (uint8_t)segment;
        job.first = (uint8_t)reg;
        submitRegisterJob(request, job);
    });

    server.on("/setreg", HTTP_GET, [](AsyncWebServerRequest *request) {
        long segment = 0, reg = 0, value = 0;

        if (!getHexParam(request, "s", 5, &segment) || !getHexParam(request, "r", 0xff, &reg) ||
            !getHexParam(request, "v", 0xff, &value)) {
            request->send(400, "application/json", F("{\"error\":\"expected s=0..5, r=0..0xff and v=0..0xff\"}"));
            return;
        }

        RegisterQueue::Job job;
        job.kind = RegisterQueue::WriteOne;
        job.token = request;
        job.segment = (uint8_t)segment;
        job.first = (uint8_t)reg;
        job.value = (uint8_t)value;
        submitRegisterJob(request, job);
    });
#endif

    // GET /freeze            report the flag
    // GET /freeze?on=1|0     freeze / unfreeze the ESP's automation
    //
    // Frozen, nothing writes a TV5725 register unless you ask.
    // Deliberately not persisted: a unit frozen into a state you cannot drive is
    // one power cycle from being usable again.
    // docs/gbs-control-debug-interface.md
#if GBS_DEBUG
    server.on("/freeze", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("on")) {
            String value = request->getParam("on")->value();
            rto->freezeAutomation = !(value == "0" || value == "false");
        }
        char body[48];
        snprintf_P(body, sizeof(body), PSTR("{\"frozen\":%s}"),
            rto->freezeAutomation ? "true" : "false");
        request->send(200, "application/json", body);
    });
#endif

    // Select an input by name, the same six the OLED menu offers. Queued for
    // loop(), so a 200 means the request was understood rather than that the
    // input is now selected -- the handlers block for seconds while detection
    // runs, and this route answers from a network callback.
    //
    // Until this existed the OLED was the ONLY way to choose an input: the six
    // handlers had two callers between them, the menu and one IR key. A unit
    // that came up on the wrong one needed someone standing at it.
#if GBS_DEBUG
    // The AV module frame on its own, so the HC32's half of the input path can
    // be asked for without moving the scaler's half in the same breath.
    //
    //   /avframe?src=ypbpr           send the frame that selects YPbPr
    //
    // Queued for loop(), which owns the UART. The reply carries the byte
    // because the HC32 acknowledges nothing and no readback reaches the ESP.
    server.on("/avframe", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("src")) {
            request->send(400, "application/json",
                "{\"error\":\"src required: rgbs rgsb vga ypbpr sv av\"}");
            return;
        }

        const String value = request->getParam("src")->value();
        const VideoSourceSelection::Id wanted = VideoSourceSelection::fromName(value.c_str());
        if (wanted == VideoSourceSelection::None) {
            request->send(400, "application/json",
                "{\"error\":\"unknown src: rgbs rgsb vga ypbpr sv av\"}");
            return;
        }

        const uint8_t frame = VideoSourceSelection::settingsFor(wanted).frame;
        pendingAvFrame = (int16_t)frame;
        char body[72];
        snprintf_P(body, sizeof(body),
            PSTR("{\"queued\":\"%s\",\"frame\":\"0x%02x\"}"),
            VideoSourceSelection::name(wanted), (unsigned)frame);
        request->send(200, "application/json", body);
    });

    // Which block still carries a signal, sampled on the device because the
    // rate is the answer and an HTTP read cannot see one.
    //
    //   /testbus?ms=25                 sweep every TEST_BUS_SEL
    //   /testbus?ms=25&sp=4            with the sync processor's vs_act_det out
    //   /testbus?ms=25&sp=4&sig=1      that stage's signal 1 rather than 0
    //   /testbus?ms=25&if=0            with an input formatter signal out
    server.on("/testbus", HTTP_GET, [](AsyncWebServerRequest *request) {
        auto number = [request](const char *name, int fallback) -> int {
            return request->hasParam(name)
                ? request->getParam(name)->value().toInt() : fallback;
        };
        pendingTestBusMs = (uint16_t)number("ms", 25);
        pendingTestBusSp = (uint8_t)number("sp", 0xff);
        pendingTestBusSig = (uint8_t)number("sig", 0);
        pendingTestBusIf = (uint8_t)number("if", 0xff);
        pendingTestBusSweep = true;
        request->send(200, "application/json", "{\"queued\":\"testbus\"}");
    });

    // The ADC sampling group, in one request, the way the firmware writes it.
    //
    //   /sampleclock                    report the group, change nothing
    //   /sampleclock?md=2039&os=2       apply a divider and an oversampling ratio
    //   /sampleclock?hold=1244          solve the whole engine around a divider
    //   /sampleclock?hold=0             release it, back to the recommendation
    //
    // Either parameter alone is enough to apply; the one left out takes what
    // the source is due.
    //
    // Queued, and it answers on the console: the group latches together, so a
    // reply written from the network callback would report registers the bus
    // has not been given a chance to write.
    // Restart the ESP. The chip keeps its registers across one, so this
    // repeats the boot rather than clearing the board -- which is what makes it
    // an instrument for a boot that comes up with no source.
    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        pendingRestart = true;
        request->send(200, "application/json", "{\"queued\":\"restart\"}");
    });

    // The coast pair, held against the engine's own constants so it can be swept
    // on a running engine. Freezing would hold it too and would stop the
    // re-solve that makes the consequence for the count visible.
    server.on("/coast", HTTP_GET, [](AsyncWebServerRequest *request) {
        const bool pre = request->hasParam("pre");
        const bool post = request->hasParam("post");
        pendingCoastClear = request->hasParam("clear");
        if (pre)
            pendingCoastPre = (uint8_t)request->getParam("pre")->value().toInt();
        if (post)
            pendingCoastPost = (uint8_t)request->getParam("post")->value().toInt();
        // Asked for nothing, report: a bare /coast that applied would carry
        // whatever the last request left behind.
        pendingCoastApply = pendingCoastClear || pre || post;
        pendingCoast = true;
        request->send(200, "application/json", "{\"queued\":\"coast\"}");
    });

    server.on("/sampleclock", HTTP_GET, [](AsyncWebServerRequest *request) {
        auto number = [request](const char *name) -> int {
            return request->hasParam(name)
                ? request->getParam(name)->value().toInt() : 0;
        };
        if (request->hasParam("hold")) {
            pendingHeldDivider = (uint16_t)number("hold");
            pendingDividerHold = true;
            request->send(200, "application/json", "{\"queued\":\"hold\"}");
            return;
        }
        pendingSampleClockApply = request->hasParam("md") || request->hasParam("os");
        pendingSampleClockDivider = (uint16_t)number("md");
        pendingSampleClockOversample = (uint8_t)number("os");
        pendingSampleClock = true;
        request->send(200, "application/json", "{\"queued\":\"sampleclock\"}");
    });

    // The input formatter's scaling-down block, which is the only minification
    // the part has. `rate` is the twelve-bit DDA increment the datasheet's own
    // formula states -- 0 is unity, 1365 is three quarters, 4095 is a half --
    // and `wanted`/`have` ask for a ratio instead.
    //
    // THE SOLVE DOES NOT CHOOSE IT YET. The capture window, both scales and
    // both output windows have to come from one decision with the ratio, and
    // what the block does to the count is measured rather than derived, so this
    // is how the bench asks. docs/scaling-down-path.md
    server.on("/inputscale", HTTP_GET, [](AsyncWebServerRequest *request) {
        auto number = [request](const char *name) -> int {
            return request->hasParam(name)
                ? request->getParam(name)->value().toInt() : 0;
        };
        char body[96];
        if (request->hasParam("wanted") || request->hasParam("have")) {
            pendingInputScaleRate =
                Tv5725::InputScale::forRatio((uint16_t)number("wanted"),
                                             (uint16_t)number("have")).increment();
        } else {
            pendingInputScaleRate = (uint16_t)number("rate");
        }
        pendingInputScale = true;
        snprintf_P(body, sizeof(body),
                   PSTR("{\"queued\":\"inputscale\",\"rate\":%u,\"held\":%u}"),
                   (unsigned)pendingInputScaleRate,
                   (unsigned)geometry.inputScale().increment());
        request->send(200, "application/json", body);
    });
#endif

#if GBS_TRACE_WRITES
    // The register write sequence, and the means to issue a slice of it back at
    // bus speed.
    //
    //   /writetrace?arm=1              record from here
    //   /writetrace?stat=1             held, seen and whether it overflowed
    //   /writetrace                    what was recorded, oldest first
    //   /writetrace?first=40&last=96   issue that slice again, back to back
    //   /writetrace?first=40&last=96&gaps=1   with the recorded spacing
    //
    // A replay from the HOST cannot answer the question this exists for:
    // /setreg is deferred to loop() and lands at tens of hertz whatever bytes it
    // carries, so a sequence whose ordering decides the outcome is not
    // reproducible from outside the device.
    server.on("/writetrace", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("arm")) {
            Tv5725::WriteTrace::arm(millis());
            request->send(200, "application/json", "{\"armed\":true}");
            return;
        }
        if (request->hasParam("stop")) {
            Tv5725::WriteTrace::stop();
            request->send(200, "application/json", "{\"armed\":false}");
            return;
        }
        if (request->hasParam("stat")) {
            char stat[96];
            snprintf(stat, sizeof(stat),
                     "{\"held\":%u,\"seen\":%lu,\"capacity\":%u,\"overflowed\":%s}",
                     (unsigned)Tv5725::WriteTrace::count(),
                     (unsigned long)Tv5725::WriteTrace::seen(),
                     (unsigned)Tv5725::WriteTrace::Capacity,
                     Tv5725::WriteTrace::overflowed() ? "true" : "false");
            request->send(200, "application/json", stat);
            return;
        }
        if (request->hasParam("first")) {
            pendingWriteReplayFirst = (uint16_t)request->getParam("first")->value().toInt();
            pendingWriteReplayLast = request->hasParam("last")
                ? (uint16_t)request->getParam("last")->value().toInt()
                : Tv5725::WriteTrace::count();
            pendingWriteReplayGaps = request->hasParam("gaps");
            pendingWriteReplay = true;
            request->send(200, "application/json", "{\"queued\":\"replay\"}");
            return;
        }

        // Fixed-width lines, so the byte index the chunked response counts in
        // divides straight into an entry number and nothing has to be carried
        // between calls.
        const uint16_t held = Tv5725::WriteTrace::count();
        const size_t total = (size_t)held * Tv5725::WriteTrace::LineBytes;
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "text/plain",
            [total](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                size_t written = 0;
                char line[Tv5725::WriteTrace::LineBytes + 1];
                while (written < maxLen && index + written < total) {
                    const size_t at = index + written;
                    Tv5725::WriteTrace::line(line, (uint16_t)(at / Tv5725::WriteTrace::LineBytes));
                    const size_t column = at % Tv5725::WriteTrace::LineBytes;
                    buffer[written++] = (uint8_t)line[column];
                }
                return written;
            });
        request->send(response);
    });
#endif
#if GBS_SAMPLING_LOG
    // Log the source measurements from loop(), where HTTP polling cannot reach:
    // at tens of hertz a host cannot tell a value that dithers from one read
    // torn across two states, nor see how long a reading takes to settle after a
    // latch -- which is the number that says how long a mode change must wait.
    //
    //   /samplinglog?ms=5&for=60000              follow a source that changes
    //   /samplinglog?low=1600&high=2900&step=100&dwell=400   walk the divider
    //
    // Queued, like /input: this answers from a network callback and must not
    // touch the bus.
    server.on("/samplinglog", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (samplingLog.active()) {
            request->send(409, "application/json", "{\"error\":\"already running\"}");
            return;
        }
        auto number = [request](const char *name, uint32_t fallback) -> uint32_t {
            return request->hasParam(name)
                ? (uint32_t)request->getParam(name)->value().toInt() : fallback;
        };

        if (request->hasParam("rates")) {
            pendingSamplingA = (uint16_t)number("rates", 200);
            pendingSamplingRates = true;
        } else if (request->hasParam("low")) {
            pendingSamplingA = (uint16_t)number("low", 1600);
            pendingSamplingB = (uint16_t)number("high", 2900);
            pendingSamplingC = (uint16_t)number("step", 100);
            pendingSamplingD = number("dwell", 400);
            pendingSamplingSweep = true;
        } else {
            pendingSamplingA = (uint16_t)number("ms", 5);
            pendingSamplingD = number("for", 60000);
            pendingSamplingMonitor = true;
        }
        request->send(200, "application/json", "{\"queued\":\"samplinglog\"}");
    });
#endif

    server.on("/input", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("src")) {
            request->send(400, "application/json",
                "{\"error\":\"src required: rgbs rgsb vga ypbpr sv av\"}");
            return;
        }

        const String value = request->getParam("src")->value();
        const VideoSourceSelection::Id wanted = VideoSourceSelection::fromName(value.c_str());
        if (wanted == VideoSourceSelection::None) {
            request->send(400, "application/json",
                "{\"error\":\"unknown src: rgbs rgsb vga ypbpr sv av\"}");
            return;
        }

        pendingInputSelection = wanted;
        char body[64];
        snprintf_P(body, sizeof(body), PSTR("{\"queued\":\"%s\"}"),
            VideoSourceSelection::name(wanted));
        request->send(200, "application/json", body);
    });

    // The output the picture lands in, named the way the mode names itself.
    // GET reports it; ?res= chooses one. A resolution is a command to scale,
    // so choosing one leaves pass-through the same way the serial letters do.
    server.on("/output", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("res")) {
            const Tv5725::OutputMode *const now = geometry.outputMode();
            const Tv5725::OutputMode *const asked = chosenOutputMode();
            char body[160];
            snprintf_P(body, sizeof(body),
                PSTR("{\"mode\":\"%s\",\"requested\":\"%s\",\"passThrough\":%s}"),
                now != NULL ? now->name() : "none",
                asked != NULL ? asked->name() : "none",
                (now != NULL && now->isBypass()) ? "true" : "false");
            request->send(200, "application/json", body);
            return;
        }

        const String value = request->getParam("res")->value();
        const Tv5725::OutputMode *const wanted =
            Tv5725::OutputMode::fromName(value.c_str());
        if (wanted == NULL) {
            request->send(400, "application/json",
                PSTR("{\"error\":\"unknown res: 1920x1080 1280x1024 1280x960 "
                     "1280x720 768x576 720x480\"}"));
            return;
        }

        pendingOutputMode = wanted;
        char body[64];
        snprintf_P(body, sizeof(body), PSTR("{\"queued\":\"%s\"}"),
            wanted->name());
        request->send(200, "application/json", body);
    });

    // The framing: the engine's only state, and the one thing writing registers
    // back cannot restore. READ ONLY -- it moves through the pads, so nothing
    // can arrange a framing the user cannot reach.
    //
    // Diagnostics: no product path reads it, only the bench instruments and the
    // hardware suite, so it goes with the rest of them at GBS_DEBUG=0. A build
    // without it answers 404 rather than reporting an empty framing.
#if GBS_DEBUG
    // Drive the described menu and read the page it would draw. The remote is
    // the only other way in, so without this a menu change cannot be judged
    // from a session at all. A press that asks for a letter queues it on the
    // surface the item names, so the tree's letters are proven against the
    // handlers that already serve /uc? and /sc?. docs/osd-menu.md
    // Press a key on the remote, from here. Both menus decode through
    // IrReceiver, so an injected key reaches whichever is live by exactly the
    // path a real press takes -- which is what lets the chain and the described
    // menu be walked and photographed side by side. docs/osd-menu.md
    server.on("/ir", HTTP_GET, [](AsyncWebServerRequest *request) {
        struct Named {
            const char *name;
            uint32_t code;
        };
        static const Named keys[] = {
            { "menu", IRKeyMenu },   { "up", IRKeyUp },
            { "down", IRKeyDown },   { "left", IRKeyLeft },
            { "right", IRKeyRight }, { "ok", IRKeyOk },
            { "exit", IRKeyExit },   { "info", IRKeyInfo },
            { "save", IRKeySave },   { "mute", IRKeyMute },
            { "volup", kRecv2 },     { "voldown", kRecv3 },
        };

        if (!request->hasParam("key")) {
            request->send(400, "application/json",
                          "{\"error\":\"key is menu up down left right ok exit "
                          "info save mute volup voldown\"}");
            return;
        }

        const String wanted = request->getParam("key")->value();
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            if (wanted == keys[i].name) {
                irrecv.inject(keys[i].code);
                String body = "{\"key\":\"";
                body += keys[i].name;
                body += "\"}";
                request->send(200, "application/json", body);
                return;
            }
        }
        request->send(400, "application/json", "{\"error\":\"unknown key\"}");
    });

    server.on("/menu", HTTP_GET, [](AsyncWebServerRequest *request) {
        Osd::MenuCommand asked;
        if (request->hasParam("key")) {
            const String key = request->getParam("key")->value();
            bool known = true;
            Osd::Menu::Key which = Osd::Menu::KeyMenu;
            if (key == "up")
                which = Osd::Menu::KeyUp;
            else if (key == "down")
                which = Osd::Menu::KeyDown;
            else if (key == "left")
                which = Osd::Menu::KeyLeft;
            else if (key == "right")
                which = Osd::Menu::KeyRight;
            else if (key == "ok")
                which = Osd::Menu::KeyOk;
            else if (key == "menu")
                which = Osd::Menu::KeyMenu;
            else if (key == "exit")
                which = Osd::Menu::KeyExit;
            else
                known = false;

            if (!known) {
                request->send(400, "application/json",
                              "{\"error\":\"key is up down left right ok menu exit\"}");
                return;
            }

            asked = describedMenu.press(which);
            queueMenuCommand(asked);
        }

        const Osd::MenuPage page = describedMenu.page();
        String body = "{\"open\":";
        body += describedMenu.isOpen() ? "true" : "false";
        body += ",\"adjusting\":";
        body += describedMenu.isAdjusting() ? "true" : "false";
        body += ",\"depth\":";
        body += describedMenu.cursor().depth();
        body += ",\"page\":{\"number\":";
        body += page.number();
        body += ",\"previous\":";
        body += page.hasPreviousPage() ? "true" : "false";
        body += ",\"next\":";
        body += page.hasNextPage() ? "true" : "false";
        body += "},\"asked\":\"";
        if (asked.asked() && asked.queue() == Osd::MenuCommand::GeometryNudge) {
            body += Tv5725::Nudge::name(asked.control());
            body += asked.direction() > 0 ? '+' : '-';
        } else if (asked.asked() && asked.queue() == Osd::MenuCommand::ValueTune) {
            body += Osd::Tune::name(asked.tuned());
            body += asked.direction() > 0 ? '+' : '-';
        } else if (asked.asked()) {
            body += asked.letter();
        }
        body += "\",\"queue\":\"";
        if (asked.asked()) {
            switch (asked.queue()) {
            case Osd::MenuCommand::UserCommand: body += "uc"; break;
            case Osd::MenuCommand::SerialCommand: body += "sc"; break;
            case Osd::MenuCommand::InputSelection: body += "input"; break;
            case Osd::MenuCommand::GeometryNudge: body += "nudge"; break;
            case Osd::MenuCommand::ValueTune: body += "tune"; break;
            }
        }
        // Which of the two trees that draw the panel holds it, read off the
        // gate itself: nothing on the board reports the panel, so this is the
        // only witness that the icon tree is staying out of a described page.
        body += "\",\"panel\":\"";
        body += NEW_OLED_MENU ? "icons" : "described";
        body += "\"";
        body += ",\"rows\":[";
        for (uint8_t row = 0; row < page.rows(); ++row) {
            const Osd::MenuItem &item = page.itemAt(row);
            const char *value = item.valueText(menuContext);
            if (row)
                body += ",";
            body += "{\"label\":\"";
            body += item.label();
            body += "\",\"value\":";
            if (value != NULL) {
                body += "\"";
                body += value;
                body += "\"";
            } else {
                body += "null";
            }
            body += ",\"selected\":";
            body += row == page.selected() ? "true" : "false";
            // Whether a press on the row would reach anything. The overlay
            // greys it and the panel says N/A; over here it is a field, since
            // neither drawing is readable from a test.
            body += ",\"available\":";
            body += page.availableAt(row) ? "true" : "false";
            body += "}";
        }
        body += "]}";
        request->send(200, "application/json", body);
    });

    server.on("/geometry", HTTP_GET, [](AsyncWebServerRequest *request) {
        char body[360];
        snprintf_P(body, sizeof(body),
            PSTR("{\"oh\":%u,\"eh\":%u,\"ov\":%u,\"ev\":%u,"
                 "\"ch\":%u,\"cv\":%u,\"fh\":%u,\"fv\":%u,"
                 "\"poh\":%d,\"peh\":%d,\"pov\":%d,\"pev\":%d,"
                 "\"lineRateHz\":%lu,\"lowLineRate\":%s,"
                 "\"aspect\":%u,\"shaped\":%s,"
                 "\"present\":%s,\"state\":\"%s\"}"),
            geometry.originUnitsOn(Tv5725::AxisHorizontal),
            geometry.extentUnitsOn(Tv5725::AxisHorizontal),
            geometry.originUnitsOn(Tv5725::AxisVertical),
            geometry.extentUnitsOn(Tv5725::AxisVertical),
            geometry.lineUnitsOn(Tv5725::AxisHorizontal),
            geometry.lineUnitsOn(Tv5725::AxisVertical),
            // The earliest unit a capture window may open on, per axis. Held
            // state, so a caller reads what the engine SOLVED against rather
            // than re-deriving it from a duty that has moved since.
            geometry.firstUnitOn(Tv5725::AxisHorizontal),
            geometry.firstUnitOn(Tv5725::AxisVertical),
            // The proportion itself, in ten-thousandths: the ESP's printf has
            // no %f, and this is the state the framing table stores.
            (int)lrintf(geometry.framing().originOn(Tv5725::AxisHorizontal) * 10000.0f),
            (int)lrintf(geometry.framing().extentOn(Tv5725::AxisHorizontal) * 10000.0f),
            (int)lrintf(geometry.framing().originOn(Tv5725::AxisVertical) * 10000.0f),
            (int)lrintf(geometry.framing().extentOn(Tv5725::AxisVertical) * 10000.0f),
            (unsigned long)inputAcquisition.sourceLineRateHz(),
            inputAcquisition.sourceLowLineRate() ? "true" : "false",
            // The shape the picture is shown in, in the same ten-thousandths
            // the framing file carries. 0 is filling.
            (unsigned)geometry.aspect().tenThousandths(),
            // False where an axis had to fill because the part cannot minify
            // into the shape. No register distinguishes that from no shape.
            geometry.shapeHonoured() ? "true" : "false",
            // The engine's own answer to "is a source there": a steadiness run
            // over the line count paired with one reading of what the sync
            // processor counts against the divider, not a live reading of
            // either. The state names which of the three, because absent and unlocked
            // want the same recovery and only one is worth re-probing the sync
            // type on. docs/video-source-acquisition.md
            inputAcquisition.sourceIsPresent() ? "true" : "false",
            inputAcquisition.sourceState() == VideoSourceAcquisition::SourceAcquired   ? "acquired"
            : inputAcquisition.sourceState() == VideoSourceAcquisition::SourceUnlocked ? "unlocked"
                                                               : "absent");
        request->send(200, "application/json", body);
    });

    // The framing table writes itself once the framing has held still, so a
    // caller that disturbs the framing and walks away persists it as that
    // source's remembered framing. `?on=1` suppresses that for the session;
    // lifting it adopts whatever is live rather than writing it.

    // Hold the framing at the whole capturable region, so one rule can be
    // checked against any source and any output resolution without a press:
    // at 100% the capture takes the source's blanking on all four sides, and
    // the scaler magnifies to fill rather than being asked to minify.
    //
    // Queued, because it re-solves: the route answers from a network callback
    // and the bus belongs to loop().
    server.on("/framing/full", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasArg("on")) {
            pendingFullFraming = request->arg("on").toInt() != 0;
            pendingFullFramingChange = true;
        }
        char body[48];
        snprintf_P(body, sizeof(body), PSTR("{\"full\":%s,\"queued\":%s}"),
                   geometry.fullFramingForced() ? "true" : "false",
                   pendingFullFramingChange ? "true" : "false");
        request->send(200, "application/json", body);
    });

    server.on("/framing/autosave", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasArg("on"))
            framingSaves.inhibit(request->arg("on").toInt() == 0);
        char body[40];
        snprintf_P(body, sizeof(body), PSTR("{\"autosave\":%s}"),
                   framingSaves.inhibited() ? "false" : "true");
        request->send(200, "application/json", body);
    });
#endif

    // Frame time lock, from held state only -- this runs in a network callback,
    // so it must not touch the bus. Whether the part AGREED to take PCLKIN is a
    // register read, and belongs on the queued /getreg path instead.
    server.on("/framesync", HTTP_GET, [](AsyncWebServerRequest *request) {
        // The phase target is the one thing here that is settable, because the
        // only instrument that can judge it is the picture.
        if (request->hasArg("phase")) {
            frameSync.setTargetPhase(request->arg("phase").toInt());
        }
        if (request->hasArg("observe")) {
            frameSync.setObserveOnly(request->arg("observe").toInt() != 0);
        }
        char body[208];
        snprintf_P(body, sizeof(body),
            PSTR("{\"ready\":%s,\"driving\":%s,\"seed\":%u,"
                 "\"targetHz\":%lu,\"nowHz\":%lu,\"fieldRateHz\":%.3f,"
                 "\"targetPhase\":%ld}"),
            frameSync.ready() ? "true" : "false",
            rto->displayClock.driving() ? "true" : "false",
            rto->displayClock.seed(),
            (unsigned long)rto->displayClock.hz(),
            (unsigned long)rto->displayClock.hzNow(),
            inputAcquisition.sourceFieldRateHz(),
            (long)frameSync.targetPhase());
        request->send(200, "application/json", body);
    });

    server.on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response =
      request->beginResponse(200, "application/json", "true");
    request->send(response);

    if (request->arg("n").length())
    {     
      if (request->arg("p").length())
      { 
        
        WiFi.begin(request->arg("n").c_str(), request->arg("p").c_str(), 0, 0, false);
      }
      else
      {
        WiFi.begin(request->arg("n").c_str(), emptyString, 0, 0, false);
      }
    }
    else
    {
      WiFi.begin();
    }
    // printf("Rest ESP\n");
    userCommand = 'u'; });

    server.on("/bin/slots.bin", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (RouteHeap::allowsAReply(request))
    {
      ensureSlotsFile();
      request->send(LittleFS, "/slots.bin", "application/octet-stream");
    } });

    server.on("/slot/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool result = false;

    if (RouteHeap::allowsAReply(request))
    {
      int params = request->params();

      if (params > 0)
      {
        AsyncWebParameter *slotParam = request->getParam(0);
        String slotParamValue = slotParam->value();
        char slotValue[2];
        slotParamValue.toCharArray(slotValue, sizeof(slotValue));
        pendingSlotSelection = (uint8_t)slotValue[0];
        result = true;
      }
    }

    request->send(200, "application/json", result ? "true" : "false"); });

    server.on("/slot/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool result = false;

    if (RouteHeap::allowsAReply(request))
    {
      int params = request->params();

      if (params > 0)
      {
        SlotMetaArray slotsObject;
        File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");

        if (slotsBinaryFileRead)
        {
          slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
          slotsBinaryFileRead.close();
        }
        else
        {
          File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");

          for (int i = 0; i < SLOTS_TOTAL; i++)
          {
            slotsObject.slot[i].slot = i;
              slotsObject.slot[i].scanlines = 0;
            slotsObject.slot[i].scanlinesStrength = 0;
            slotsObject.slot[i].wantVdsLineFilter = false;
            slotsObject.slot[i].wantStepResponse = true;
            slotsObject.slot[i].wantPeaking = true;
            char emptySlotName[25] = "Empty                   ";
            strncpy(slotsObject.slot[i].name, emptySlotName, 25);
          }

          slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
          slotsBinaryFileWrite.close();
        }

        AsyncWebParameter *slotIndexParam = request->getParam(0);
        String slotIndexString = slotIndexParam->value();
        uint8_t slotIndex = lowByte(slotIndexString.toInt());
        if (slotIndex >= SLOTS_TOTAL)
        {
          goto fail;
        }

        AsyncWebParameter *slotNameParam = request->getParam(1);
        String slotName = slotNameParam->value();

        char emptySlotName[25] = "                        ";
        strncpy(slotsObject.slot[slotIndex].name, emptySlotName, 25);

        slotsObject.slot[slotIndex].slot = slotIndex;
        slotName.toCharArray(slotsObject.slot[slotIndex].name, sizeof(slotsObject.slot[slotIndex].name));
        slotsObject.slot[slotIndex].scanlines = uopt->wantScanlines;
        slotsObject.slot[slotIndex].scanlinesStrength = uopt->scanlineStrength;
        slotsObject.slot[slotIndex].wantVdsLineFilter = uopt->wantVdsLineFilter;
        slotsObject.slot[slotIndex].wantStepResponse = uopt->wantStepResponse;
        slotsObject.slot[slotIndex].wantPeaking = uopt->wantPeaking;

        File slotsBinaryOutputFile = LittleFS.open(SLOTS_FILE, "w");
        slotsBinaryOutputFile.write((byte *)&slotsObject, sizeof(slotsObject));
        slotsBinaryOutputFile.close();

        // The grid's save button is what a user reaches for, so the framing
        // goes in here as well as on /uc?4. Held state only -- the framing and
        // the key the engine already has -- so this stays off the bus.
        storeSlotFraming((int16_t)slotIndex);

        result = true;
      }
    }

fail:
    request->send(200, "application/json", result ? "true" : "false"); });

    server.on("/slot/remove", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool result = false;
    int params = request->params();
    AsyncWebParameter *p = request->getParam(0);
    char param = p->name().charAt(0);
    if (params > 0)
    {
      if (param == '0')
      {
        result = true;
      }
      else
      {
        const int16_t currentSlot = currentSlotIndex();
        if (currentSlot < 0) {
            request->send(200, "application/json", "false");
            return;
        }

        SlotMetaArray slotsObject;
        ensureSlotsFile();
        File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");
        slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
        slotsBinaryFileRead.close();

        // Removing a slot closes the gap, so everything after it moves down
        // one: the name, the picture settings and the framings file alike.
        // `.slot` is the position rather than a stored value, so it stays.
        for (int i = currentSlot; i < SLOTS_TOTAL - 1; ++i) {
            SlotMeta &into = slotsObject.slot[i];
            const SlotMeta &from = slotsObject.slot[i + 1];
            into.scanlines = from.scanlines;
            into.scanlinesStrength = from.scanlinesStrength;
            into.wantVdsLineFilter = from.wantVdsLineFilter;
            into.wantStepResponse = from.wantStepResponse;
            into.wantPeaking = from.wantPeaking;
            into.slot = i;
            strncpy(into.name, from.name, 25);

            LittleFS.remove(slotFramingPath(i));
            LittleFS.rename(slotFramingPath(i + 1), slotFramingPath(i));
        }

        SlotMeta &last = slotsObject.slot[SLOTS_TOTAL - 1];
        last.slot = SLOTS_TOTAL - 1;
        last.scanlines = 0;
        last.scanlinesStrength = 0;
        last.wantVdsLineFilter = false;
        last.wantStepResponse = true;
        last.wantPeaking = true;
        strncpy(last.name, EMPTY_SLOT_NAME, 25);
        LittleFS.remove(slotFramingPath(SLOTS_TOTAL - 1));

        File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");
        slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
        slotsBinaryFileWrite.close();

        result = true;
      }
    }

    request->send(200, "application/json", result ? "true" : "false"); });

    server.on("/slot/remove", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool result = false;
    int params = request->params();
    AsyncWebParameter *p = request->getParam(0);
    char param = p->name().charAt(0);
    if (params > 0)
    {
      if (param == '0')
      {
        result = true;
      }
      else
      {
        Ascii8 slot = uopt->presetSlot;
        Ascii8 nextSlot;
        auto currentSlot = slotIndexMap.indexOf(slot);

        SlotMetaArray slotsObject;
        File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");
        slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
        slotsBinaryFileRead.close();
        String slotName = slotsObject.slot[currentSlot].name;

        
        uint8_t loopCount = 0;
        uint8_t flag = 1;
        while (flag != 0)
        {
          slot = slotIndexMap[currentSlot + loopCount];
          nextSlot = slotIndexMap[currentSlot + loopCount + 1];
          flag = 0;

          slotsObject.slot[currentSlot + loopCount].slot = slotsObject.slot[currentSlot + loopCount + 1].slot;
          slotsObject.slot[currentSlot + loopCount].scanlines = slotsObject.slot[currentSlot + loopCount + 1].scanlines;
          slotsObject.slot[currentSlot + loopCount].scanlinesStrength = slotsObject.slot[currentSlot + loopCount + 1].scanlinesStrength;
          slotsObject.slot[currentSlot + loopCount].wantVdsLineFilter = slotsObject.slot[currentSlot + loopCount + 1].wantVdsLineFilter;
          slotsObject.slot[currentSlot + loopCount].wantStepResponse = slotsObject.slot[currentSlot + loopCount + 1].wantStepResponse;
          slotsObject.slot[currentSlot + loopCount].wantPeaking = slotsObject.slot[currentSlot + loopCount + 1].wantPeaking;
          
          strncpy(slotsObject.slot[currentSlot + loopCount].name, slotsObject.slot[currentSlot + loopCount + 1].name, 25);
          loopCount++;
        }

        File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");
        slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
        slotsBinaryFileWrite.close();

        // Or the next slot named here inherits framings nobody stored for it.
        LittleFS.remove(slotFramingPath(currentSlot));
        result = true;
      }
    }

fail:
    request->send(200, "application/json", result ? "true" : "false"); });

    server.on("/fs/upload", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "application/json", "true"); });

    server.on(
        "/fs/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            request->send(200, "application/json", "true");
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                request->_tempFile = LittleFS.open("/" + filename, "w");
            }
            if (len) {
                request->_tempFile.write(data, len);
            }
            if (final) {
                request->_tempFile.close();
            }
        });

    server.on("/fs/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (RouteHeap::allowsAReply(request))
    {
      int params = request->params();
      if (params > 0)
      {
        request->send(LittleFS, request->getParam(0)->value(), String(), true);
      }
      else
      {
        request->send(200, "application/json", "false");
      }
    } });

    server.on("/fs/dir", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (RouteHeap::allowsAReply(request))
    {
      Dir dir = LittleFS.openDir("/");
      String output = "[";

      while (dir.next())
      {
        // Two differences from SPIFFS, both of which reach the web UI:
        //
        // LittleFS emits synthetic "." and ".." entries before any real file,
        // so an unfiltered listing offers the UI two names it cannot download.
        // isFile() drops them.
        //
        // And fileName() returns the bare name where SPIFFS returned the full
        // path. The UI feeds these straight back to /fs/download?file=,
        // so the leading slash is part of this endpoint's contract and is put
        // back here rather than letting the listing change shape.
        if (!dir.isFile())
        {
          continue;
        }
        output += "\"/";
        output += dir.fileName();
        output += "\",";
        delay(1);
      }

      output += "]";

      output.replace(",]", "]");

      request->send(200, "application/json", output);
    } });

    // Remove ONE file. /fs/format was the only way to delete anything and it
    // takes /preferences.txt and /slots.bin with it -- and /fs/upload is a
    // stub that writes nothing, so a format cannot be undone.
    //
    // Deliberately general: it will delete the preferences too. Losing those
    // costs a set of defaults, and a route that silently refused some paths
    // would be worse than one that does what it is told and says so.
    server.on("/fs/rm", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool result = false;

    if (request->params() > 0)
    {
      String path = request->getParam(0)->value();

      // A bare "/" is the root, not a file, and LittleFS::remove() on it is not
      // something to find out about the hard way.
      if (path.length() > 1 && path.startsWith("/") && LittleFS.exists(path))
      {
        result = LittleFS.remove(path);
        SerialM.printf("fs/rm: %s %s\n", path.c_str(), result ? "removed" : "FAILED");
      }
      else
      {
        SerialM.printf("fs/rm: %s not removed, no such file\n", path.c_str());
      }
    }

    request->send(200, "application/json", result ? "true" : "false"); });

    server.on("/fs/format", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(200, "application/json", LittleFS.format() ? "true" : "false"); });

    // Outside GBS_DEBUG on purpose: the build whose identity matters most is a
    // release one, and gating this would leave exactly that one anonymous.
    server.on("/version", HTTP_GET, [](AsyncWebServerRequest *request) {
        char body[80];
        snprintf_P(body, sizeof(body), PSTR("{\"rev\":\"%s\"}"), GBS_BUILD_REV);
        request->send(200, "application/json", body);
    });

    // The boot trace, from RAM. Read after a mains-only cold start with nothing
    // attached -- the boot that could not be watched over serial.
#if GBS_DEBUG
    server.on("/bootlog", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Streamed, not copied into a String: building a 2 KB String to answer a
        // diagnostic request allocates more heap than this unit has spare.
        String header = "free heap: " + String(ESP.getFreeHeap()) + " bytes\n";
#if BOOTLOG_BYTES == 0
        // Distinguishable from "empty" on purpose: empty is a regression, and
        // disabled is a build choice.
        header += F("(boot log disabled: rebuild with BOOTLOG_BYTES=2048)\n");
        request->send(200, "text/plain", header);
        return;
#else
        if (bootLogLen == 0) {
            header += F("(boot log empty)\n");
            request->send(200, "text/plain", header);
            return;
        }
#endif
        if (BOOTLOG_BYTES > 0 && bootLogLen >= BOOTLOG_BYTES - 1) {
            header += F("(TRUNCATED: buffer full, raise BOOTLOG_BYTES)\n");
        }

        const size_t headerLen = header.length();
        const size_t total = headerLen + bootLogLen;
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "text/plain",
            [header, headerLen, total](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                if (index >= total) {
                    return 0;
                }
                size_t written = 0;
                while (written < maxLen && index + written < total) {
                    const size_t at = index + written;
                    buffer[written] = (at < headerLen)
                        ? (uint8_t)header[at]
#if BOOTLOG_BYTES > 0
                        : (uint8_t)bootLog[at - headerLen];
#else
                        : (uint8_t)0;   // unreachable: bootLogLen is always 0
#endif
                    written++;
                }
                return written;
            });
        request->send(response);
    });
#endif
#endif

    server.on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    WiFiMode_t wifiMode = WiFi.getMode();
    request->send(200, "application/json", wifiMode == WIFI_AP ? "{\"mode\":\"ap\"}" : "{\"mode\":\"sta\",\"ssid\":\"" + WiFi.SSID() + "\"}"); });

    persWM.setConnectNonBlock(true);
    if (WiFi.SSID().length() == 0) {
        persWM.setupWiFiHandlers();
        persWM.startApMode();
    } else {
        persWM.begin();
    }

    server.begin();
    webSocket.begin();
    yield();

#ifdef HAVE_PINGER_LIBRARY

    pinger.OnReceive([](const PingerResponse &response) {
    if (response.ReceivedResponse)
{
      Serial.printf(
        "Reply from %s: time=%lums\n",
        response.DestIPAddress.toString().c_str(),
        response.ResponseTime);

      pingLastTime = millis() - 900; 
    }
    else
    {
      Serial.printf("Request timed out.\n");
    }

    
    
    return true; });

    pinger.OnEnd([](const PingerResponse &response) { return true; });
#endif
}

void initUpdateOTA()
{
    ArduinoOTA.setHostname("GBS OTA");

    ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else 
      type = "filesystem";

    
    LittleFS.end();
    ; });
    ArduinoOTA.onEnd([]() { ; });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { ; });
    ArduinoOTA.onError([](ota_error_t error) {
    ;
    if (error == OTA_AUTH_ERROR)
      ;
    else if (error == OTA_BEGIN_ERROR)
      ;
    else if (error == OTA_CONNECT_ERROR)
      ;
    else if (error == OTA_RECEIVE_ERROR)
      ;
    else if (error == OTA_END_ERROR)
      ; });
    ArduinoOTA.begin();
    yield();
}


// = (uint8_t)(f.read() - '0');
void loadFramingTable()
{
    framingIsSuspect = true;

    if (!LittleFS.exists(FramingFilePath)) {
        // Nothing stored yet is not a failed read. Every source takes its
        // computed default and the first tuning is saveable.
        framingIsSuspect = false;
        framingSaves.markSaved(sourceFramings.revision());
        return;
    }

    File f = LittleFS.open(FramingFilePath, "r");
    if (!f)
        return;

    Tv5725::FramingTable read;
    Tv5725::FramingText text(read);
    char line[80];
    while (f.available()) {
        const String next = f.readStringUntil('\n');
        strncpy(line, next.c_str(), sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        text.readLine(line);
    }
    f.close();

    for (uint16_t i = 0; i < read.count(); ++i)
        sourceFramings.remember(read.keyAt(i), read.framingAt(i));

    framingIsSuspect = false;
    framingSaves.markSaved(sourceFramings.revision());
}

void saveFramingTable()
{
    if (framingIsSuspect) {
        printf("not saving framings: this boot could not read them\n");
        return;
    }

    File f = LittleFS.open(FramingFilePath, "w");
    if (!f)
        return;

    f.print(F("# framing, one source a line: "
              "<lines>@<fieldRateHz>/<syncWidth><hPol><vPol> = "
              "originH extentH originV extentV shape\n"
              "# in ten-thousandths of the capturable region; shape 0 fills\n"));

    Tv5725::FramingText text(sourceFramings);
    char line[80];
    for (uint16_t i = 0; i < sourceFramings.count(); ++i)
        if (text.writeLine(i, line, sizeof(line))) {
            f.print(line);
            f.print('\n');
        }
    f.close();

    framingSaves.markSaved(sourceFramings.revision());
}

// Which slot the user has selected, or -1 when the preference names none.
int16_t currentSlotIndex()
{
    return (int16_t)slotIndexMap.indexOf((char)uopt->presetSlot);
}

// A slot is a NAMED COPY OF THE FRAMING TABLE. Saving one copies the file the
// per-source framings already live in; loading one copies it back and reloads
// it. That bounds the data by the number of slots rather than by RAM, which is
// what a table of every slot's framings could not be: the whole table fits in
// one file and only one is ever in memory. docs/framing-presets.md
static String slotFramingPath(int16_t slot)
{
    return String(F("/slot-")) + String((int)slot) + String(F(".txt"));
}

static bool copyFile(const String &from, const String &to)
{
    File in = LittleFS.open(from, "r");
    if (!in)
        return false;

    File out = LittleFS.open(to, "w");
    if (!out) {
        in.close();
        return false;
    }

    uint8_t buffer[128];
    while (in.available()) {
        const size_t got = in.read(buffer, sizeof(buffer));
        if (got == 0)
            break;
        out.write(buffer, got);
    }
    in.close();
    out.close();
    return true;
}

// The framings as they stand, under this slot's name. The debounced save is
// flushed first, or the copy is of whatever the table held when it last went
// quiet rather than of what is on screen.
bool storeSlotFraming(int16_t slot)
{
    if (slot < 0)
        return false;
    saveFramingTable();
    if (!LittleFS.exists(FramingFilePath))
        return false;
    return copyFile(String(FramingFilePath), slotFramingPath(slot));
}

// This slot's framings become the framings. Restored through the engine, which
// re-solves every register from them.
bool recallSlotFraming(int16_t slot)
{
    if (slot < 0)
        return false;
    const String path = slotFramingPath(slot);
    if (!LittleFS.exists(path))
        return false;
    if (!copyFile(path, String(FramingFilePath)))
        return false;

    sourceFramings.clear();
    loadFramingTable();

    Tv5725::PanAndZoom framing;
    Tv5725::Aspect shape;
    if (!sourceFramings.find(geometry.framedKey(), &framing, &shape))
        return false;
    geometry.setAspect(shape);
    return geometry.applyFraming(framing);
}

// Choosing a slot IS loading it: a slot holds what the user stored for every
// source, so arriving on one puts the picture where that slot left it for the
// source in front of the chip. A slot holding nothing for it leaves the
// picture alone. docs/framing-presets.md
bool applySelectedSlot()
{
    const int16_t slot = currentSlotIndex();
    if (slot < 0)
        return false;

    SlotMetaArray slotsObject;
    File f = LittleFS.open(SLOTS_FILE, "r");
    if (f && f.size() == sizeof(SlotMetaArray)) {
        f.read((byte *)&slotsObject, sizeof(slotsObject));
        f.close();

        const SlotMeta &meta = slotsObject.slot[slot];
        uopt->wantScanlines = meta.scanlines;
        uopt->scanlineStrength = meta.scanlinesStrength;
        uopt->wantVdsLineFilter = meta.wantVdsLineFilter;
        uopt->wantStepResponse = meta.wantStepResponse;
        uopt->wantPeaking = meta.wantPeaking;

        if (!uopt->wantScanlines)
            disableScanlines();
        geometry.applyPictureFilters(uopt->wantVdsLineFilter, uopt->wantPeaking);
        geometry.applyOutputPictureFilters(uopt->wantSharpness,
                                           uopt->wantStepResponse);
    } else if (f) {
        f.close();
    }

    return recallSlotFraming(slot);
}

// Called every loop. Nothing is written until the table has held still.
void pollFramingSave(uint32_t now)
{
    if (framingSaves.due(sourceFramings.revision(), now, FramingSaveQuietMs))
        saveFramingTable();
}

void saveUserPrefs()
{
    // Refuse if this boot never managed to read the file. Otherwise the first
    // setting the user touches -- or any of the several paths that save as a
    // side effect -- persists a full set of defaults over settings that are
    // still perfectly good on flash. That is how the loss actually happened:
    // not one bad write, but a silent bad read followed by an ordinary save.
    if (prefsAreSuspect) {
        debugPrintf("not saving settings: this boot could not read them\n");
        return;
    }

    File f = LittleFS.open(SettingsFilePath, "w");
    if (!f) {
        return;
    }

    // "w" alone has been measured leaving a tail from whatever wrote the file
    // longer. The terminator makes one inert, but a settings file is read by
    // people as well as by this firmware. docs/known-issues.md
    f.truncate(0);

    f.print(F("# GBSC-Pro settings, one key = value a line. A missing key takes\n"
              "# its default and an unknown one is ignored, so adding or removing\n"
              "# a setting cannot shift another. The last line is what says the\n"
              "# file was written whole. docs/preferences-file.md\n"));

    char line[80];
    for (uint16_t i = 0; settings.writeLine(i, line, sizeof(line)); ++i) {
        f.print(line);
        f.print('\n');
    }
    f.print(Prefs::Settings::terminator());
    f.print('\n');
    f.close();

    if (LittleFS.exists(LegacySettingsPath)) {
        LittleFS.remove(LegacySettingsPath);
    }
}

// The two screens the remote reaches that are not menu rows: the volume overlay
// and Info. Both are driven by a key rather than by the cursor, and both are
// what OSD_IR() points oled_menuItem at. docs/osd-menu.md
void drawOverlayScreens()
{

    if (oled_menuItem == 0) {
        NEW_OLED_MENU = true;
    } else {
        NEW_OLED_MENU = false;
    }



    if (oled_menuItem == 1) {
        // Only when the level moved: display() pushes the whole panel
        // framebuffer over the bus the acquisition shares, and this branch runs
        // on every pass the overlay is up. Invalidated when the overlay opens,
        // or reopening at an unchanged level would draw nothing.
        adl = 50 - Volume;
        if (adl != volumeShown) {
            volumeShown = adl;
            if (OLED_clear_flag)
                display.clear();
            OLED_clear_flag = ~0;
            display.setColor(OLEDDISPLAY_COLOR::WHITE);
            display.setTextAlignment(TEXT_ALIGN_LEFT);
            display.setFont(ArialMT_Plain_16);
            display.drawString(8, 15, "Volume - / + dB");
            display.display();
            Osd::VolumeOverlay::draw(adl);
        }

        if (irrecv.decode(&results)) {
            decode_flag = 1;
            switch (results.value) {
                case kRecv2: // ++
                    Volume = MAX(Volume - 1, 0);
                    adl = 50 - Volume;
                    PT_2257(Volume + 12);
                    break;
                case kRecv3: // --
                    Volume = MIN(Volume + 1, 50);
                    adl = 50 - Volume;
                    PT_2257(Volume + 12);
                    break;
                case IRKeyMenu:
                    oled_menuItem = 0;
                    OSD_clear();
                    describedMenu.open();
                    break;

                case IRKeyOk:
                    saveUserPrefs();
                    break;

                case IRKeyExit:
                    oled_menuItem = 0;
                    OSD_clear();
                    break;
            }
            irrecv.resume();
        }
    }

    else if (oled_menuItem == 152) {
        // ONCE A SECOND, NOT ONCE A PASS. Reading the output frame rate TIMES
        // PULSES on the debug pin, and in pass-through there is no VDS pulse to
        // time, so it waits out its timeout -- measured, that took a register
        // read from 0.03 s to 1.2..5.2 s and starved OTA to the point of
        // reading as a wedged firmware. The remote still answers every pass.
        static unsigned long drawnAt = 0;
        if (millis() - drawnAt >= InfoScreenRefreshMs) {
        drawnAt = millis();
        if (OLED_clear_flag)
            display.clear();
        OLED_clear_flag = ~0;
        display.setColor(OLEDDISPLAY_COLOR::WHITE);
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        display.setFont(ArialMT_Plain_16);
        display.drawString(1, 0, "Menu-");
        display.drawString(1, 28, "Info");
        display.display();

        // EVERY NUMBER IS ASKED FOR, NONE MEASURED HERE. The scan type comes
        // from the answer SourceMeasurement already reached: measuring it feeds
        // a steadiness run the acquisition layer steers on, and a draw at the
        // redraw cadence would be a second owner of it. The chain classified
        // the source again through STATUS_IF_INP_*, the input formatter's SD
        // classifier, and printed Err for everything it did not recognise --
        // which on an RGB computer source is every frame.
        // NOTHING HERE MEASURES. Every number is state the engine already
        // holds: the output mode, the source key it solved for and the scan
        // type SourceMeasurement last reached. The rate is the key's, which is
        // also the output's, because the raster is solved for it.
        Osd::InfoScreen::Report report;
        const Tv5725::OutputMode *const out = geometry.outputMode();
        report.bypass = out == NULL || out->isBypass();
        report.outputPx = report.bypass ? 0 : out->activePx();
        report.outputLines = report.bypass ? 0 : out->activeLines();

        const VideoSourceSelection::Id selected = VideoSourceSelection::selected();
        report.input = VideoSourceSelection::shownName(selected);
        report.separateSync = !geometry.syncTypeIsCsync();
        report.present = inputAcquisition.sourceIsPresent()
                         && Tv5725::Chip::hasPower() && Info_sate != 1;

        if (!report.present)
            report.kind = Osd::InfoScreen::NoInput;
        else if (selected == VideoSourceSelection::Ypbpr)
            report.kind = Osd::InfoScreen::Component;
        else if (selected == VideoSourceSelection::SVideo)
            report.kind = Osd::InfoScreen::SVideo;
        else if (selected == VideoSourceSelection::Composite)
            report.kind = Osd::InfoScreen::Composite;
        else
            report.kind = Osd::InfoScreen::Rgb;

        const Tv5725::SourceKey key = geometry.reportedKey();
        report.lines = key.lines();
        report.rateHz = (uint8_t)(key.rateHz() + 0.5f);
        report.interlaced =
            sourceSampling.scanType() == Tv5725::SourceMeasurement::ScanInterlaced;
        report.lineRateHz = geometry.sourceLineRateHz();
        Osd::InfoScreen::draw(report);
        }

        if (irrecv.decode(&results)) {
            decode_flag = 1;
            switch (results.value) {
                case IRKeyMenu:
                    oled_menuItem = 0;
                    OSD_clear();
                    describedMenu.open();
                    break;
                // Info dismisses the screen Info opened, so the key that
                // shows it is the key that takes it away.
                case IRKeyInfo:
                case IRKeyExit:
                    if (Info_sate) {
                        GBS::VDS_DIS_HB_ST::write(St);
                        GBS::VDS_DIS_HB_SP::write(Sp);
                        Info_sate = 0;
                    }
                    oled_menuItem = 0;
                    OSD_clear();
                    break;
            }
            irrecv.resume();
        }
    }

    if (
        (
            (results.value == IRKeyMenu) ||
            (results.value == IRKeySave) ||
            (results.value == IRKeyInfo) ||
            (results.value == IRKeyRight) ||
            (results.value == IRKeyLeft) ||
            (results.value == IRKeyUp) ||
            (results.value == IRKeyDown) ||
            (results.value == IRKeyOk) ||
            (results.value == IRKeyExit) ||
            (results.value == IRKeyMute) ||
            (results.value == kRecv2) ||
            (results.value == kRecv3)) &&
        (decode_flag == 1) &&
        (oled_menuItem != 0)) {
        // printf("Delay success \n");
        Tim_menuItem = millis();
        decode_flag = 0;
        OledUpdataTime = 1;
    }

    if (oled_menuItem_last != oled_menuItem && oled_menuItem != 0) {
        Tim_menuItem = millis();
        OLED_clear_flag = 1;
        // printf("freq:%d \n", system_get_cpu_freq());
        // printf("oled_menuItem:%d \n", oled_menuItem);
        // printf("Info_sate:%d \n", Info_sate);
    }
    if ((millis() - Tim_menuItem) >= OSD_CLOSE_TIME && oled_menuItem != 0) {
        // 菜单关闭
        if (Info_sate) {
            GBS::VDS_DIS_HB_ST::write(St);
            GBS::VDS_DIS_HB_SP::write(Sp);
            Info_sate = 0;
        }
        oled_menuItem = 0;
        oled_menuItem_last = 0;
        OSD_clear();
    }
    oled_menuItem_last = oled_menuItem;
}

// What a remote key does outside the menu: the volume overlay, Mute and the
// Info screen. Takes a frame already decoded rather than reading one, because
// decode() is one-shot -- whichever caller reads it first is the only one that
// can, so the menu forwards what it does not answer instead of leaving it.
static void handleRemoteKey()
{
    decode_flag = 1;
    if (results.value == IRKeyMenu) {
        Tim_menuItem = millis();
        if (rto->sourceDisconnected || !Tv5725::Chip::hasPower() || GBS::PAD_CKIN_ENZ::read()) // || !GBS::STATUS_MISC_VSYNC::read()
        {

            NEW_OLED_MENU = false;
            oled_menuItem = 152;

            // InputINFO();
            //////////new
            Info_sate = 1;
            St = GBS::VDS_DIS_HB_ST::read();
            Sp = GBS::VDS_DIS_HB_SP::read();

            /////////new
            // loadDefaultUserOptions();
            loadComputedPreset(&Tv5725::Mode480p); 
            doPostPresetLoadSteps();
            GBS::VDS_DIS_HB_ST::write(0x00);
            GBS::VDS_DIS_HB_SP::write(0xffff);
            Tv5725::FrameBuffer::freezeCapture();                  
            GBS::SP_CLAMP_MANUAL::write(1); 
                                            // GBS::VDS_U_OFST::write(GBS::VDS_U_OFST::read() + 100);
        } else {
            NEW_OLED_MENU = false;
            oled_menuItem = 0;
            describedMenu.open();
            display.clear();
        }
    }

    if (results.value == IRKeyInfo) {
        Tim_menuItem = millis();
        NEW_OLED_MENU = false;
        oled_menuItem = 152;
    }

    switch (results.value) {
        case IRKeyMute: {
            // The chain drew the row 800 times over to hold it on screen, which
            // blocked loop() for seconds and painted the panel 800 times with
            // it. One draw and one dwell says the same thing.
            Tim_menuItem = millis();
            MUTE_R = MUTE_R == 0 ? 1 : 0;
            PT_MUTE(MUTE_R ? 0x79 : 0x78);
            NEW_OLED_MENU = false;
            Osd::MuteOverlay::draw(MUTE_R != 0);
            display.clear();
            display.setTextAlignment(TEXT_ALIGN_LEFT);
            display.setFont(ArialMT_Plain_16);
            display.drawString(8, 15, MUTE_R ? "MUTE ON" : "MUTE OFF");
            display.display();
            delay(MuteOverlayDwellMs);
            oled_menuItem = 0;
            OSD_clear();
            break;
        }
        case kRecv2:
        case kRecv3:
            Tim_menuItem = millis();
            NEW_OLED_MENU = false;
            volumeShown = -1;
            oled_menuItem = 1;
            break;
    }

}

void OSD_IR()
{
    if (irrecv.decode(&results)) {
        handleRemoteKey();
        irrecv.resume();
        delay(5);
    }
}

