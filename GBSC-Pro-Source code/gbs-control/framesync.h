#ifndef FRAMESYNC_H_
#define FRAMESYNC_H_

// fast digitalRead()
#if defined(ESP8266)
#define digitalRead(x) ((GPIO_REG_READ(GPIO_IN_ADDRESS) >> x) & 1)
#ifndef DEBUG_IN_PIN
#define DEBUG_IN_PIN D6
#endif
#else // Arduino
// fastest, but non portable (Uno pin 11 = PB3, Mega2560 pin 11 = PB5)
// #define digitalRead(x) bitRead(PINB, 3)
#include "fastpin.h"
#define digitalRead(x) fastRead<x>()
// no define for DEBUG_IN_PIN
#endif

#include <ESP8266WiFi.h>

// Included here rather than relied on from the .ino: framesync.h uses
// Clock::ClockRamp in setExternalClockGenFrequencySmooth, and depending on the
// sketch to include it first works only while the ordering happens to hold.
#include "src/clock/ClockGen.h"

// FS_DEBUG:      full verbose debug over serial
// FS_DEBUG_LED:  just blink LED (off = adjust phase, on = normal phase)
// #define FS_DEBUG
// #define FS_DEBUG_LED
// fsDebugPrintf: framesync's per-decision trace (HTotal search, sync phase,
// vsync period), routed through SerialM so it reaches the web console.
#if GBS_DEBUG
#define fsDebugPrintf(...) SerialM.printf(__VA_ARGS__)
#else
#define fsDebugPrintf(...)
#endif

// How long sampleVsyncPeriod() waits for its two edges before giving up.
//
// It needs one edge to arm and a second to measure, so the worst case is two
// frame periods -- 40 ms at 50 Hz -- plus the delay(7). 250 ms is comfortably
// above that and still far short of the interval that drops WiFi.
#define FS_SAMPLE_TIMEOUT_MS 250

// Spins between deadline checks. The wait has to stay a tight poll on a
// volatile: millis() and ESP.wdtFeed() are function calls, and doing both on
// every pass slows it enough to stop a measurement completing. Checking every
// 1024 spins costs nothing and still bounds the wait to within a fraction of a
// millisecond.
#define FS_SAMPLE_CHECK_EVERY 1024

// When the input sample times out, poll DEBUG_IN_PIN directly for this long and
// report what it did. Every register that gates the pin can read correct --
// PAD_BOUT_EN, TEST_BUS_EN, TEST_BUS_SEL, PAD_TRI_ENZ -- and the edges still not
// arrive, and configuration cannot tell you which side is at fault. A level that
// never moves says the chip is not driving it or the ESP is not seeing it; a
// level that moves says the edges are there and the ISR path is the problem.
//
// 25 ms is over one frame at 50 Hz, so a working vsync must show transitions.
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
        // startTime = ESP.getCycleCount();
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
        // stopTime = ESP.getCycleCount();
        __asm__ __volatile__("rsr %0,ccount"
                             : "=a"(stopTime));
        detachInterrupt(DEBUG_IN_PIN);
        interrupts();
    }
}

// Walk the external clock to `freq`, in Clock::ClockRamp's steps.
//
// The stepping policy is Clock::ClockRamp's, where it is host-tested. The loop
// always finishes ON the target: a clock left even 500 Hz out shows as a
// rolling bar.
void setExternalClockGenFrequencySmooth(uint32_t freq)
{
    uint32_t current = rto->freqExtClockGen;
    rto->freqExtClockGen = freq;

    // handleWiFi is passed in as the pump: a slew of up to 750 steps is 750 I2C
    // transactions, long enough that WiFi and the watchdog need servicing, and
    // dropping that turns a frequency change into a reboot.
    clockGen.slewTo(current, freq, [] { handleWiFi(0); });
}

template <class GBS, class Attrs>
class FrameSyncManager
{
private:
    typedef typename GBS::STATUS_VDS_VERT_COUNT VERT_COUNT;
    typedef typename GBS::VDS_HSYNC_RST HSYNC_RST;
    typedef typename GBS::VDS_VSYNC_RST VSYNC_RST;
    typedef typename GBS::VDS_VS_ST VSST;
    typedef typename GBS::template Tie<VSYNC_RST, VSST> VRST_SST;

    static const uint8_t debugInPin = Attrs::debugInPin;
    static const int16_t syncCorrection = Attrs::syncCorrection;
    static const int32_t syncTargetPhase = Attrs::syncTargetPhase;

    static bool syncLockReady;
    static uint8_t delayLock;
    static int16_t syncLastCorrection;

    /// Set to -1 if uninitialized.
    /// Reset with syncLastCorrection.
    static float maybeFreqExt_per_videoFps;


#if GBS_DEBUG
    /// Poll DEBUG_IN_PIN and report whether it moves at all.
    ///
    /// Only meaningful after a sample has already failed, and only called from
    /// there. The edge ISR is detached by then -- sampleVsyncPeriod() calls
    /// MeasurePeriod::stop() before it returns false -- so this is a plain read
    /// of the pin and cannot disturb a measurement in flight.
    static void probeDebugPin()
    {
        static uint32_t lastProbe = 0;
        const uint32_t now = millis();
        if (lastProbe != 0 && (int32_t)(now - (lastProbe + FS_PROBE_INTERVAL_MS)) < 0)
        {
            return;
        }
        lastProbe = now;

        // Sweep the selectors this firmware uses elsewhere, so a pin that is
        // simply on the wrong bus can be told from one that is dead. 0x0 is what
        // framesync measures on, 0x2 is VDS, 0xa is what the sync watcher and
        // the HTotal search use. If none of them move it, the fault is the pin
        // or the net, not the selection.
        const uint8_t selectors[] = {0x0, 0x2, 0xa};
        const uint8_t selBackup = GBS::TEST_BUS_SEL::read();
        const uint8_t enBackup = GBS::TEST_BUS_EN::read();

        GBS::TEST_BUS_EN::write(1);

        for (uint8_t i = 0; i < sizeof(selectors); i++)
        {
            GBS::TEST_BUS_SEL::write(selectors[i]);
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

            fsDebugPrintf(
                "  DEBUG_IN_PIN sel=0x%x: %u transitions in %ums, level %d->%d, %u samples\n",
                selectors[i], transitions, (unsigned)FS_PROBE_MS, first, level, spins);
        }

        GBS::TEST_BUS_SEL::write(selBackup);
        GBS::TEST_BUS_EN::write(enBackup);
    }
#endif

    // Sample input and output vsync periods and their phase
    // difference in microseconds
    static bool vsyncPeriodAndPhase(int32_t *periodInput, int32_t *periodOutput, int32_t *phase)
    {
        fsDebugPrintf("vsyncPeriodAndPhase(), TEST_BUS_SEL=%d\n", GBS::TEST_BUS_SEL::read());

        uint32_t inStart, inStop, outStart, outStop;
        uint32_t inPeriod, outPeriod, diff;

        // calling code needs to ensure debug bus is ready to sample vperiod

        if (!sampleVsyncPeriod(&inStart, &inStop))
        {
            fsDebugPrintf("vsyncPeriodAndPhase(): no INPUT vsync\n");
#if GBS_DEBUG
            probeDebugPin();
#endif
            return false;
        }

        GBS::TEST_BUS_SEL::write(0x2); // 0x2 = VDS (t3t50t4) // measure VDS vblank (VB ST/SP)
        inPeriod = (inStop - inStart); //>> 1;
        if (!sampleVsyncPeriod(&outStart, &outStop))
        {
            fsDebugPrintf("vsyncPeriodAndPhase(): no OUTPUT vsync\n");
            return false;
        }
        outPeriod = (outStop - outStart); //>> 1;

        diff = (outStart - inStart) % inPeriod;
        if (periodInput)
            *periodInput = inPeriod;
        if (periodOutput)
            *periodOutput = outPeriod;
        if (phase)
            *phase = (diff < inPeriod) ? diff : diff - inPeriod;

        return true;
    }

    static bool sampleVsyncPeriods(uint32_t *input, uint32_t *output)
    {
        int32_t inPeriod, outPeriod;

        if (!vsyncPeriodAndPhase(&inPeriod, &outPeriod, NULL))
            return false;

        *input = inPeriod;
        *output = outPeriod;

        return true;
    }

    // Find appropriate htotal that makes output frame time slightly more than the input.
    static bool findBestHTotal(uint32_t &bestHtotal)
    {
        uint16_t inHtotal = HSYNC_RST::read();
        uint32_t inPeriod, outPeriod;

        if (inHtotal == 0)
        {
            return false;
        } // safety
        if (!sampleVsyncPeriods(&inPeriod, &outPeriod))
        {
            return false;
        }

        if (inPeriod == 0 || outPeriod == 0)
        {
            return false;
        } // safety

        // allow ~4 negative (inPeriod is < outPeriod) clock cycles jitter
        if ((inPeriod > outPeriod ? inPeriod - outPeriod : outPeriod - inPeriod) <= 4)
        {
            /*if (inPeriod >= outPeriod) {
        Serial.print("inPeriod >= out: ");
        Serial.println(inPeriod - outPeriod);
      }
      else {
        Serial.print("inPeriod < out: ");
        Serial.println(outPeriod - inPeriod);
      }*/
            bestHtotal = inHtotal;
        }
        else
        {
            // large htotal can push intermediates to 33 bits
            bestHtotal = (uint64_t)(inHtotal * (uint64_t)inPeriod) / (uint64_t)outPeriod;
        }

        // new 08.11.19: skip this step, IF period measurement should be stable enough to give repeatable results
        // if (bestHtotal == (inHtotal + 1)) { bestHtotal -= 1; } // works well
        // if (bestHtotal == (inHtotal - 1)) { bestHtotal += 1; } // check with SNES + vtotal = 1000 (1280x960)

#ifdef FS_DEBUG
        if (bestHtotal != inHtotal)
        {
            Serial.print(F("                     wants new htotal, oldbest: "));
            Serial.print(inHtotal);
            Serial.print(F(" newbest: "));
            Serial.println(bestHtotal);
            Serial.print(F("                     inPeriod: "));
            Serial.print(inPeriod);
            Serial.print(F(" outPeriod: "));
            Serial.println(outPeriod);
        }
#endif
        return true;
    }

public:
    // Time one period of whatever signal the debug pin currently carries.
    //
    // Which signal that is belongs to the caller: TEST_BUS_SEL selects it, and
    // vsyncPeriodAndPhase() switches from input to output vsync between its two
    // calls. Nothing here depends on the choice.
    //
    // **THE WAIT IS BOUNDED IN TIME, AND THE WATCHDOG STAYS RUNNING.** Bounding
    // it by loop passes instead, with the watchdog off, holds the CPU long
    // enough that serial, ping and HTTP all die while the picture keeps running
    // -- the TV5725 is a separate chip -- and the caller re-enters immediately,
    // so a bounded stall behaves like a permanent wedge. A PLLAD_MD write big
    // enough to break sync is exactly how you get here.
    //
    // Deliberately no yield() in the spin. The timestamps come from the two
    // ICACHE_RAM_ATTR edge ISRs reading ccount, so this loop is a pure wait --
    // but yield() runs the WiFi stack, whose interrupts-off sections would
    // delay an edge ISR and skew the timestamp it records. FrameSync resolves
    // the period to about one cycle in three million, and a few thousand cycles
    // of added interrupt latency would swamp that. The delay(7) after the first
    // edge stays exactly where it is: it yields in the ~20 ms of slack between
    // edges, well away from the one that is about to be measured.
    static bool sampleVsyncPeriod(uint32_t *start, uint32_t *stop)
    {
        yield();
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
                // Signed difference, so this still terminates across the
                // millis() wrap rather than spinning for another 49 days.
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

        if ((*start >= *stop) || *stop == 0 || *start == 0)
        {
            // ESP.getCycleCount() overflow oder no pulse, just fail this round
            return false;
        }

        return true;
    }

    // sets syncLockReady = ready() = false, which in turn starts a new init()
    // -> findBestHtotal() run in loop()
    static void reset(uint8_t frameTimeLockMethod)
    {
#ifdef FS_DEBUG
        Serial.print("FS reset(), with correction: ");
#endif
        if (syncLastCorrection != 0)
        {
#ifdef FS_DEBUG
            Serial.println("Yes");
#endif
            uint16_t vtotal = 0, vsst = 0;
            VRST_SST::read(vtotal, vsst);
            uint16_t timeout = 0;
            vtotal -= syncLastCorrection;
            if (frameTimeLockMethod == 0)
            { // moves VS position
                vsst -= syncLastCorrection;
            }

            while ((GBS::STATUS_VDS_FIELD::read() == 1) && (++timeout < 400))
                ;
            GBS::VDS_VS_ST::write(vsst);
            timeout = 0;
            while ((GBS::STATUS_VDS_FIELD::read() == 0) && (++timeout < 400))
                ;
            GBS::VDS_VSYNC_RST::write(vtotal);
        }
#ifdef FS_DEBUG
        else
        {
            Serial.println("No");
        }
#endif
        fsDebugPrintf("FrameSyncManager::reset(%d)\n", frameTimeLockMethod);

        syncLockReady = false;
        syncLastCorrection = 0;
        delayLock = 0;
        // Don't clear maybeFreqExt_per_videoFps.
        //
        // Clearing is unsafe, since many callers call reset(), don't
        // call externalClockGenSyncInOutRate() -> initFrequency(), then
        // expect runFrequency() to keep working.
        //
        // Not clearing is hopefully safe, since when loading an output
        // resolution, externalClockGenResetClock() calls
        // FrameSync::clearFrequency() and clears the variable, and
        // later someone calls externalClockGenSyncInOutRate() ->
        // FrameSync::initFrequency().
    }

    static void resetWithoutRecalculation()
    {
        syncLockReady = false;
        delayLock = 0;
    }

    static uint16_t init()
    {
        uint32_t bestHTotal = 0;

        // Adjust output horizontal sync timing so that the overall
        // frame time is as close to the input as possible while still
        // being less.  Increasing the vertical frame size slightly
        // should then push the output frame time to being larger than
        // the input.
        if (!findBestHTotal(bestHTotal))
        {
            return 0;
        }

        syncLockReady = true;
        delayLock = 0;
        return (uint16_t)bestHTotal;
    }

    static uint32_t getPulseTicks()
    {
        uint32_t inStart, inStop;
        if (!sampleVsyncPeriod(&inStart, &inStop))
        {
            return 0;
        }
        return inStop - inStart;
    }

    static bool ready(void)
    {
        return syncLockReady;
    }

    static int16_t getSyncLastCorrection()
    {
        return syncLastCorrection;
    }

    static void cleanup()
    {
        fsDebugPrintf("FrameSyncManager::cleanup(), resetting video frequency\n");

        syncLastCorrection = 0; // the important bit
        syncLockReady = 0;
        delayLock = 0;

        // 我们应该清除 maybeFreqExt_per_videoFps 吗？
        //
        // 希望清除是安全的。
        // 在 15 kHz 和 31 kHz 输入之间切换时调用，或
        // 当长时间没有视频且 // 输出关闭时。
        // 输出关闭。(在 240p 和 480i 之间切换时，cleanup() 不会被调用。
        // 在 240p 和 480i 之间切换时不会调用 cleanup()）。当出现新的视频信号时、
        // 有人会调用 externalClockGenSyncInOutRate() -> // FrameSync::initial().
        // 帧同步::initFrequency() 来重新初始化输出帧。
        // 同步。
        //
        // 希望不清零是安全的。参见 reset()
        // 解释。



        maybeFreqExt_per_videoFps = -1;
    }


    // Perform vsync phase locking.  This is accomplished by measuring
    // the period and phase offset of the input and output vsync
    // signals and adjusting the frame size (and thus the output vsync
    // frequency) to bring the phase offset closer to the desired
    // value.
    static bool runVsync(uint8_t frameTimeLockMethod)
    {
        int32_t period;
        int32_t phase;
        int32_t target;
        int16_t correction;

        if (!syncLockReady)
            return false;

        if (delayLock < 2)
        {
            delayLock++;
            return true;
        }

        if (!vsyncPeriodAndPhase(&period, NULL, &phase))
            return false;

        target = (syncTargetPhase * period) / 360;

        if (phase > target)
            correction = 0;
        else
            correction = syncCorrection;

#ifdef FS_DEBUG
        Serial.printf("phase: %7d target: %7d", phase, target);
        if (correction == syncLastCorrection)
        {
            // terminate line if returning early
            Serial.println();
        }
#endif
#ifdef FS_DEBUG_LED
        if (correction == 0)
        {
            digitalWrite(15, LOW); // LED ON
        }
        else
        {
            digitalWrite(15, HIGH); // LED OFF
        }
#endif

        // return early?
        if (correction == syncLastCorrection)
        {
            return true;
        }

        int16_t delta = correction - syncLastCorrection;
        uint16_t vtotal = 0, vsst = 0;
        uint16_t timeout = 0;
        VRST_SST::read(vtotal, vsst);
        vtotal += delta;
        if (frameTimeLockMethod == 0)
        { // moves VS position
            vsst += delta;
        }
        // else it is method 1: leaves VS position alone

        while ((GBS::STATUS_VDS_FIELD::read() == 1) && (++timeout < 400))
            ;
        GBS::VDS_VS_ST::write(vsst);
        timeout = 0;
        while ((GBS::STATUS_VDS_FIELD::read() == 0) && (++timeout < 400))
            ;
        GBS::VDS_VSYNC_RST::write(vtotal);

        syncLastCorrection = correction;

#ifdef FS_DEBUG
        Serial.printf("  vtotal: %4d\n", vtotal);
#endif

        return true;
    }

    static void clearFrequency()
    {
        maybeFreqExt_per_videoFps = -1;
    }

    static void initFrequency(float outFramesPerS, uint32_t freqExtClockGen)
    {
        /*
        This value can be interpreted in multiple ways:

        - Each output frame is a fixed number of video clocks long, at a
          given output resolution.
        - At a given output resolution, the video clock rate should be
          proportional to the input FPS.
        */
        maybeFreqExt_per_videoFps = (float)freqExtClockGen / outFramesPerS;
    }

    // Perform vsync phase locking.  This is accomplished by measuring
    // the period and phase offset of the input and output vsync
    // signals, then adjusting the output video clock to bring the phase
    // offset closer to the desired value.
    static bool runFrequency()
    {
        if (maybeFreqExt_per_videoFps < 0)
        {
            ; // SerialM.printf(
              //  "Error: trying to tune external clock frequency while clock frequency uninitialized!\n");
            return true;
        }

        // Compare to externalClockGenSyncInOutRate().
        if (GBS::PAD_CKIN_ENZ::read() != 0)
        {
            // Failed due to external factors (PAD_CKIN_ENZ=0 on
            // startup), not bad input signal, don't return frame sync
            // error.
            fsDebugPrintf(
                "Skipping FrameSyncManager::runFrequency(), GBS::PAD_CKIN_ENZ::read() != 0\n");
            return true;
        }

        if (rto->outModeHdBypass)
        {
            fsDebugPrintf(
                "Skipping FrameSyncManager::runFrequency(), rto->outModeHdBypass\n");
            return true;
        }
        if (GBS::PLL648_CONTROL_01::read() != 0x75)
        {
            ; //SerialM.printf(\
                "Error: trying to tune external clock frequency while set to internal clock, PLL648_CONTROL_01=%d!\n",\
                GBS::PLL648_CONTROL_01::read());
            return true;
        }

        if (!syncLockReady)
        {
            fsDebugPrintf(
                "Skipping FrameSyncManager::runFrequency(), !syncLockReady\n");
            return false;
        }

        // ESP32 FPU only accelerates single-precision float add/mul, not divide, not double.
        // https://esp32.com/viewtopic.php?p=82090#p82090

        // ESP CPU cycles/s
        const float esp8266_clock_freq = ESP.getCpuFreqMHz() * 1000000;

        // ESP CPU cycles
        int32_t periodInput; // int32_t periodOutput;
        int32_t phase;

        // Frame/s
        float fpsInput;

        // Measure input period until we get two consistent measurements. This
        // substantially reduces the chance of incorrectly guessing FPS when
        // input sync changes (but does not eliminate it, eg. when resetting a
        // SNES).
        bool success = false;
        for (int attempt = 0; attempt < 2; attempt++)
        {
            // Measure input period and output latency.
            bool ret = vsyncPeriodAndPhase(&periodInput, nullptr, &phase);
            // TODO make vsyncPeriodAndPhase() restore TEST_BUS_SEL, not the caller?
            GBS::TEST_BUS_SEL::write(0x0);
            if (!ret)
            {
                fsDebugPrintf("runFrequency(): attempt %d: vsyncPeriodAndPhase failed\n", attempt);
                continue;
            }

            fpsInput = esp8266_clock_freq / (float)periodInput;
            if (fpsInput < 47.0f || fpsInput > 86.0f)
            {
                fsDebugPrintf("runFrequency(): attempt %d: fpsInput out of range: %f\n", attempt, fpsInput);
                continue;
            }

            // Measure input period again. vsyncPeriodAndPhase()/getPulseTicks()
            // -> sampleVsyncPeriod() depend on GBS::TEST_BUS_SEL = 0, but
            // vsyncPeriodAndPhase() sets it to 2.
            GBS::TEST_BUS_SEL::write(0x0);
            uint32_t periodInput2 = getPulseTicks();
            if (periodInput2 == 0)
            {
                fsDebugPrintf("runFrequency(): attempt %d: getPulseTicks failed\n", attempt);
                continue;
            }
            float fpsInput2 = esp8266_clock_freq / (float)periodInput2;
            if (fpsInput2 < 47.0f || fpsInput2 > 86.0f)
            {
                fsDebugPrintf("runFrequency(): attempt %d: fpsInput2 out of range: %f\n", attempt, fpsInput2);
                continue;
            }

            // Check that the two FPS measurements are sufficiently close.
            float diff = fabs(fpsInput2 - fpsInput);
            float relDiff = diff / std::min(fpsInput, fpsInput2);
            if (relDiff != relDiff || diff > 0.5f || relDiff > 0.00833f)
            {
                fsDebugPrintf("runFrequency(): attempt %d: inconsistent FPS %f vs %f\n", attempt, fpsInput, fpsInput2);
                continue;
            }

            success = true;
            break;
        }
        if (!success)
        {
            fsDebugPrintf("runFrequency(): gave up after %d attempts\n", 2);
            return false;
        }

        // ESP CPU cycles
        int32_t target = (syncTargetPhase * periodInput) / 360;

        // Latency error (distance behind target), in fractional frames.
        // If latency increases, boost frequency, and vice versa.
        const float latency_err_frames =
            (float)(phase - target) // cycles
            / esp8266_clock_freq    // s
            * fpsInput;             // frames

        // 0.0038f is 2/525, the difference between SNES and Wii 240p.
        // This number is somewhat arbitrary, but works well in
        // practice.
        float correction = 0.0038f * latency_err_frames;

        // Some LCD displays (eg. Dell U2312HM) lose sync when changing
        // frequency by 0.1% (switching between 59.94 and 60 FPS).
        //
        // To ensure long-term FPS stability, clamp the maximum deviation from
        // input FPS to 0.06%. This is sufficient as long as fpsInput does not
        // vary drastically from frame to frame.
        constexpr float MAX_CORRECTION = 0.0006f;
        if (correction > MAX_CORRECTION)
            correction = MAX_CORRECTION;
        if (correction < -MAX_CORRECTION)
            correction = -MAX_CORRECTION;

        const float rawFpsOutput = fpsInput * (1 + correction);

        // This has floating-point conversion round-trip rounding errors, which
        // is suboptimal, but it's not a big deal.
        const float prevFpsOutput = (float)rto->freqExtClockGen / maybeFreqExt_per_videoFps;

        // In case fpsInput is measured incorrectly, rawFpsOutput may be
        // drastically different from the previous frame's output FPS. To limit
        // the impact of incorrect input FPS measurements, clamp the maximum FPS
        // deviation relative to the previous frame's *output* FPS. This
        // provides short-term FPS stability.
        constexpr float MAX_FPS_CHANGE = 0.0006f;
        float fpsOutput = rawFpsOutput;
        fpsOutput = std::min(fpsOutput, prevFpsOutput * (1 + MAX_FPS_CHANGE));
        fpsOutput = std::max(fpsOutput, prevFpsOutput * (1 - MAX_FPS_CHANGE));

        if (fabs(rawFpsOutput - prevFpsOutput) >= 1.f)
        {
            ; //SerialM.printf(\
                "FPS excursion detected! Measured input FPS %f, previous output FPS %f",\
                fpsInput, prevFpsOutput);
        }

        fsDebugPrintf(
            "periodInput=%d, fpsInput=%f, latency_err_frames=%f from %f, "
            "fpsOutput=%f := %f\n",
            periodInput, fpsInput, latency_err_frames, (float)syncTargetPhase / 360.f,
            prevFpsOutput, fpsOutput);

        const auto freqExtClockGen = (uint32_t)(maybeFreqExt_per_videoFps * fpsOutput);

        fsDebugPrintf(
            "Setting clock frequency from %u to %u\n",
            rto->freqExtClockGen, freqExtClockGen);

        setExternalClockGenFrequencySmooth(freqExtClockGen);
        return true;
    }
};

// grrrrrrrrr

template <class GBS, class Attrs>
int16_t FrameSyncManager<GBS, Attrs>::syncLastCorrection;

template <class GBS, class Attrs>
float FrameSyncManager<GBS, Attrs>::maybeFreqExt_per_videoFps;

template <class GBS, class Attrs>
uint8_t FrameSyncManager<GBS, Attrs>::delayLock;

template <class GBS, class Attrs>
bool FrameSyncManager<GBS, Attrs>::syncLockReady;
#endif
