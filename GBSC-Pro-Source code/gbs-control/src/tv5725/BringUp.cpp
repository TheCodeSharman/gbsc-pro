#include "BringUp.h"

#include "Adc.h"
#include "Chip.h"
#include "FrameBuffer.h"
#include "Gpio.h"
#include "InputFormatter.h"
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
    Gpio::init();            // s0  pin mux
    Interrupts::init();      // s0  masks and their resets
    InputFormatter::init();  // s1  input path, H-sync rate, line double
    VideoProcessor::init();             // s3  scaler filters and coefficients
    MemoryBus::init();       // s4  SDRAM controller and bus timing
    FrameBuffer::init();     // s4  memory map, guards, both FIFOs
    Adc::init();             // s5  ADC trim and its PLL
    SyncProcessor::init();   // s5  thresholds and windows
}

}  // namespace Tv5725
