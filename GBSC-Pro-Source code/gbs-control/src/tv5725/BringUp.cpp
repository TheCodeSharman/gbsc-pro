#include "BringUp.h"

#include "Adc.h"
#include "Chip.h"
#include "Deinterlacer.h"
#include "FrameBuffer.h"
#include "Gpio.h"
#include "HdBypass.h"
#include "InputFormatter.h"
#include "ModeDetect.h"
#include "Interrupts.h"
#include "MemoryBus.h"
#include "SyncProcessor.h"
#include "VideoProcessor.h"

namespace Tv5725 {

void BringUp::init()
{
    // ADDRESS ORDER. Chip stays FIRST: it releases the six block resets that
    // everything below is configured behind, and releasing one afterwards would
    // discard that configuration. See BringUp.h.
    Chip::init();            // s0  resets, pads, DAC routing, output clock
    HdBypass::init();        // s0  the bypass block held off; scaling avoids it
    Gpio::init();            // s0  pin mux
    Interrupts::init();      // s0  masks and their resets
    InputFormatter::init();  // s1  input path, H-sync rate, line double
    ModeDetect::init();      // s1  the thresholds a standard is named by
    Deinterlacer::init();    // s2  motion adaptive deinterlacer and diagonal bob
    VideoProcessor::init();             // s3  scaler filters and coefficients
    MemoryBus::init();       // s4  SDRAM controller and bus timing
    FrameBuffer::init();     // s4  memory map, guards, both FIFOs
    Adc::init();             // s5  ADC trim and its PLL
    SyncProcessor::init();   // s5  thresholds and windows
}

void BringUp::holdAllBlocks()
{
    Chip::SFTRST_IF_RSTZ::write(0);
    Chip::SFTRST_DEINT_RSTZ::write(0);
    Chip::SFTRST_MEM_FF_RSTZ::write(0);
    Chip::SFTRST_MEM_RSTZ::write(0);
    Chip::SFTRST_FIFO_RSTZ::write(0);
    Chip::SFTRST_OSD_RSTZ::write(0);
    Chip::SFTRST_VDS_RSTZ::write(0);
    Chip::SFTRST_DEC_RSTZ::write(0);
    Chip::SFTRST_MODE_RSTZ::write(0);
    Chip::SFTRST_SYNC_RSTZ::write(0);
    Chip::SFTRST_INT_RSTZ::write(0);
    HdBypass::hold();
}

}  // namespace Tv5725
