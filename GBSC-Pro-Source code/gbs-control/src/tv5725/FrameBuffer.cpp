#include "FrameBuffer.h"

#include "MemoryMap.h"
namespace Tv5725 {

void FrameBuffer::init()
{
    // --- what the tables transcribed, and nobody has explained -------------
    //
    // Constants copied from the twelve preset tables, agreeing across all of
    // them, with no derivation available: nobody knows why RFF_WFF_STA_ADDR_B is
    // 1. FIRST, so the computed memory map below wins wherever the two meet.
    CAP_CNTRL_TST::write(0x0);                   // s4_20[2:0]
    CAP_FF_HALF_REQ::write(0x1);                 // s4_21[1:1]
    CAP_BUF_STA_INV::write(0x0);                 // s4_21[2:2]
    CAP_DOUBLE_BUFFER::write(0x0);               // s4_21[3:3]
    CAP_VRST_FFRST_EN::write(0x1);               // s4_21[6:6]
    CAP_ADR_ADD_2::write(0x0);                   // s4_21[7:7]
    CAP_LAST_POP_CTL::write(0x0);                // s4_22[2:2]
    CAP_REQ_FREEZ::write(0x0);                   // s4_22[3:3]
    PB_BYPASS::write(0x0);                       // s4_2b[3:3]
    PB_DB_FIELD_EN::write(0x0);                  // s4_2b[4:4]
    PB_DB_BUFFER_EN::write(0x0);                 // s4_2b[5:5]
    PB_2FRAME_EXCHG::write(0x0);                 // s4_2b[6:6]
    PB_ENABLE::write(0x1);                       // s4_2b[7:7]
    PB_UP_DOW_RBUF_INV::write(0x0);              // s4_2e[0:0]
    PB_UP_DOW_RBUF_SEL::write(0x0);              // s4_2e[1:1]
    PB_DOUBLE_REFRESH_EN::write(0x0);            // s4_2e[7:7]
    PB_TST_REG::write(0x0);                      // s4_2f[3:0]
    PB_CAP_NOISE_CMD::write(0x0);                // s4_30[3:0]
    PB_CAP_BUF_STA_ADDR_C::write(0x0);           // s4_3b[20:0]
    PB_CAP_BUF_STA_ADDR_D::write(0x3);           // s4_3e[20:0]
    WFF_TST_REG::write(0x0);                     // s4_41[7:0]
    WFF_VRST_FF_RST::write(0x0);                 // s4_42[4:4]
    WFF_ADR_ADD_2::write(0x1);                   // s4_42[5:5]
    WFF_REQ_OVER::write(0x1);                    // s4_42[6:6]
    WFF_FF_STATUS_SEL::write(0x0);               // s4_42[7:7]
    WFF_FF_STATUS::write(0x0);                   // s4_43[7:0]
    WFF_YUV_DEINTERLACE::write(0x0);             // s4_4a[0:0]
    WFF_LAST_POP_CTL::write(0x0);                // s4_4a[7:7]
    WFF_HB_DELAY::write(0x4);                    // s4_4b[2:0]
    WFF_VB_DELAY::write(0x1);                    // s4_4b[6:4]
    RFF_NEW_PAGE::write(0x0);                    // s4_4d[3:0]
    RFF_TST_REG::write(0x0);                     // s4_50[3:0]
    RFF_WFF_STA_ADDR_B::write(0x1);              // s4_54[20:0]

    // --- and what MemoryMap derives ------------------------------------
    // The deinterlacer's field store, from address 0. WFF_SAFE_GUARD is 1 in all
    // twelve tables, so only the guard's address was ever a preset's to choose.
    RFF_WFF_STA_ADDR_A::write(MemoryMap::FieldStoreStart);
    WFF_SAFE_GUARD_A::write(MemoryMap::FieldStoreGuard);
    WFF_SAFE_GUARD_B::write(MemoryMap::FieldStoreGuard);
    WFF_SAFE_GUARD::write(1);

    // The capture buffer. _B mirrors _A rather than naming a second buffer --
    // CAP_DOUBLE_BUFFER, PB_DB_BUFFER_EN and PB_DB_FIELD_EN all read 0, so it is
    // inert, but a pair left disagreeing is a trap for whoever enables double
    // buffering next.
    PB_CAP_BUF_STA_ADDR_A::write(MemoryMap::CaptureStart);
    PB_CAP_BUF_STA_ADDR_B::write(MemoryMap::CaptureStart);

    // The chip's own bound on the capture buffer, which no preset ever switched
    // on. The engine already clamps the capture to fit (MemoryMap::clampWidth);
    // this is the hardware saying it underneath, on a failure whose symptom is a
    // wrong address on screen and no report anywhere.
    CAP_SAFE_GUARD_A::write(MemoryMap::CaptureGuard);
    CAP_SAFE_GUARD_B::write(MemoryMap::CaptureGuard);
    CAP_SAFE_GUARD_EN::write(1);

    // The FIFO request watermarks: MASTER_FLAG sets the HIGH request timing and
    // GENERAL_FLAG the LOW, for the playback FIFO feeding the VDS and the read
    // FIFO behind it. RD-5725-1.1 gives no units, range or formula, so these are
    // constants. The tables are not a derivation either -- pal_240p ships
    // PB_MAST_FLAG_REG 34 against pal_1920x1080's 24 at eight times the pixel
    // rate -- so one set for every mode, pal_1920x1080's, the highest output
    // bandwidth of the twelve and the set in the glitch-free bench state. Treat
    // a change here as a bench change: PB_FETCH_NUM is the same family and tore
    // the picture across 80 of 493 framings.
    PB_MAST_FLAG_REG::write(24);
    PB_GENERAL_FLAG_REG::write(61);
    RFF_MASTER_FLAG::write(36);
    RFF_GENERAL_FLAG::write(60);

    // WFF_FF_HALF_REQ raises a request at half full rather than at the
    // programmed flag, and only ntsc_240p and ntsc_downscale set it.
    // WFF_LINE_FLIP inverts the line id, which is the deinterlacer's business
    // rather than a watermark's.
    WFF_FF_HALF_REQ::write(0);
    WFF_LINE_FLIP::write(1);

    // The deinterlacer's FIFO settings, written with the deinterlacer off: its
    // enable/disable pair are the only other writers and neither runs on a
    // progressive source. Stride and request priority are the same in all twelve
    // tables and in the enable path, so there is nothing to derive.
    RFF_ADR_ADD_2::write(1);
    RFF_REQ_SEL::write(3);

    // These two vary across the tables only because ntsc_1280x720 ships the
    // deinterlacer-on values. 1 and 1 is the off state, which the other eleven
    // tables and disableMotionAdaptDeinterlace() agree on independently, and the
    // deinterlacer still overrides it when it is switched on.
    RFF_FETCH_NUM::write(1);
    WFF_FF_STA_INV::write(1);
}

}  // namespace Tv5725
