// Host-compiled coverage for the whole static bring-up -- `make -C test bringup`.
//
// `--dump` prints every register the bring-up touches, in the order it touches
// them, so an empty diff across a refactor is the behaviour-preservation proof
// CODING_STYLE.md asks for:
//
//     ./output/test_bringup --dump > /tmp/before
//     ...change something...
//     ./output/test_bringup --dump | diff /tmp/before -
//
// ORDER IS PART OF THE CONTRACT: Chip writes the six SFTRST_*_RSTZ fields that
// release the deinterlacer, memory, FIFO, OSD and interrupt blocks, and
// releasing a block's reset after configuring it discards the configuration. So
// the dump is a sequence, not a set.
//
// The seam is test/fake/Wire.h: Tv5725.h is header-only C++ whose only Arduino
// dependency is Wire, so compiling against the fake bus needs no #ifdef in the
// firmware.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "SketchSeam.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/BringUp.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

// Neither a preset table's value nor the firmware's, so a register left at the
// poison was never written rather than written with the value we hoped for.
static const uint8_t Poison = 0xA5;

// Every write the bring-up performs, in order, recorded by the bus itself.
// A field is written through read-modify-write, so one field can appear as
// several entries at the same address -- which is exactly what wants checking.
struct Write {
    uint8_t segment;
    uint8_t reg;
    uint8_t value;
};

static std::vector<Write> runBringUp()
{
    Wire.reset();
    Wire.poison(Poison);
    Wire.trace.clear();
    Tv5725::BringUp::init();

    std::vector<Write> out;
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        const FakeTwoWire::Traced &t = Wire.trace[i];
        if (t.reg == FakeTwoWire::SegmentRegister)
            continue;  // the segment pointer is bus plumbing, not configuration
        Write w = {t.segment, t.reg, t.value};
        out.push_back(w);
    }
    return out;
}

// The value a field holds after the bring-up -- but only if the bring-up put it
// there. The run is repeated under two COMPLEMENTARY poisons and a field the two
// disagree about is reported as NotWritten, which no field can hold.
//
// One poison proves nothing where its bits already match the wanted value:
// SP_HS_POL_ATO wants 0 and 0xA5 leaves that bit clear, so a read-back passes
// whether or not anything wrote it, and `touched` cannot separate them either
// because three other fields in the byte are written regardless.
static const uint32_t NotWritten = 0xFFFFFFFFu;

// The same question asked through the field's own typedef. A hand-written
// address does not error, it returns a plausible number, so the only safe way
// to name a field in a test is the name.
#define WRITTEN(Field) \
    written(Field::segment, Field::byteOffset, Field::bitOffset, Field::bitWidth)

static uint32_t written(uint8_t segment, uint8_t reg, uint8_t offset,
                        uint8_t width)
{
    const uint8_t poisons[2] = {Poison, static_cast<uint8_t>(~Poison)};
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(poisons[i]);
        Tv5725::BringUp::init();
        under[i] = Wire.field(segment, reg, offset, width);
    }
    return under[0] == under[1] ? under[0] : NotWritten;
}

// The bring-up run under one poison, as a final byte per address. A byte is not
// a unit of ownership -- every write is read-modify-write, so a touched byte
// still carries the poison wherever no field writes -- which is why the caller
// chooses the poison: two complementary dumps, and the bits that agree are the
// mask the block owns. test_geometry_pads.py compares that against a live chip.
static void dumpFinalState(uint8_t poison)
{
    Wire.reset();
    Wire.poison(poison);
    Tv5725::BringUp::init();

    for (uint8_t segment = 0; segment < FakeTwoWire::Segments; ++segment)
        for (int reg = 0; reg < 256; ++reg)
            if (Wire.touched[segment][reg])
                std::printf("s%u_%02x = 0x%02x\n", segment,
                            static_cast<unsigned>(reg), Wire.bank[segment][reg]);
}

static void dumpWrites()
{
    std::vector<Write> writes = runBringUp();
    for (size_t i = 0; i < writes.size(); ++i)
        std::printf("s%u_%02x = 0x%02x\n", writes[i].segment, writes[i].reg,
                    writes[i].value);
    std::printf("# %u writes\n", static_cast<unsigned>(writes.size()));
}

int main(int argc, char **argv)
{
    // Before the test runner, which exits non-zero on an option it does not
    // know.
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        dumpWrites();
        return 0;
    }
    // --final-state <poison>: the settled byte per address rather than the write
    // sequence. Run twice with complementary poisons to recover which BITS the
    // block owns; see dumpFinalState().
    if (argc > 2 && std::strcmp(argv[1], "--final-state") == 0) {
        dumpFinalState(static_cast<uint8_t>(std::strtoul(argv[2], 0, 0)));
        return 0;
    }
    return doctest::Context(argc, argv).run();
}

TEST_CASE("the bring-up writes something to every segment it configures")
{
    runBringUp();

    // s0 chip, s1 input formatter and mode detect, s2 deinterlacer, s3 VDS,
    // s4 memory and FIFOs, s5 ADC and sync processor. Every segment the chip
    // has.
    const uint8_t configured[] = {0, 1, 2, 3, 4, 5};
    for (size_t i = 0; i < sizeof(configured) / sizeof(configured[0]); ++i) {
        uint8_t segment = configured[i];
        bool any = false;
        for (int reg = 0; reg < 256; ++reg)
            any = any || Wire.touched[segment][reg];
        CAPTURE(segment);
        CHECK(any);
    }
}

TEST_CASE("holding every block holds every block, including HD bypass")
{
    // The three sites that wrote RESET_CONTROL_0x46 and _0x47 as 0x00 meant
    // this. Spelling it as two byte writes hid that s0_46[7] and s0_47[7:5] are
    // RESERVED and were being forced to 0 along with the eleven real fields --
    // and hid that one of those fields is now HdBypass's.
    Wire.reset();
    Wire.poison(0xFF);
    Tv5725::BringUp::holdAllBlocks();

    const uint8_t held46[] = {0, 1, 2, 3, 4, 5, 6};
    for (size_t i = 0; i < sizeof(held46) / sizeof(held46[0]); ++i) {
        CAPTURE(held46[i]);
        CHECK(Wire.field(0, 0x46, held46[i], 1) == 0);
    }
    for (uint8_t bit = 0; bit <= 4; ++bit) {
        CAPTURE(bit);
        CHECK(Wire.field(0, 0x47, bit, 1) == 0);
    }

    // The RESERVED bits are left as found, which a byte write cannot do.
    CHECK(Wire.field(0, 0x46, 7, 1) == 1);
    CHECK(Wire.field(0, 0x47, 5, 3) == 0x7);
}

TEST_CASE("the deinterlacer is configured after its reset is released")
{
    // SFTRST_DEINT_RSTZ is s0_46[1], written 1 by Chip::init() to RELEASE the
    // deinterlacer, and a reset released after the block is configured discards
    // the configuration. While the deinterlacer block was loaded from
    // writeProgramArrayNew() it ran BEFORE Chip::init() and nothing could see
    // it: the constraint is between two classes, so no per-class test reaches
    // it and the whole-bring-up ordering test only knew about segments 0 to 5
    // minus 2.
    std::vector<Write> writes = runBringUp();

    size_t deintReleased = writes.size();
    size_t firstDeintConfig = writes.size();
    for (size_t i = 0; i < writes.size(); ++i) {
        if (writes[i].segment == 0 && writes[i].reg == 0x46)
            deintReleased = i;
        if (writes[i].segment == 2 && i < firstDeintConfig)
            firstDeintConfig = i;
    }

    CHECK(deintReleased < firstDeintConfig);
}

TEST_CASE("the display PLL is brought out of reset on every preset load")
{
    // s0_43[5] PLL_VCORST, and s0_40[2] PLL_IS beside it. setResetParameters()
    // and runSyncWatcher() both ASSERT the reset and neither clears it; the
    // preset table did, shipping s0_43 = 0x19 in all twelve. Without it the
    // display PLL is held in reset: no output clock, no signal, and every other
    // register reading correct.
    //
    // The byte IS touched, by PLL_LEN in bit 4, so asking about the byte would
    // have passed throughout. Only the field can tell.
    runBringUp();

    CHECK(Wire.field(0, 0x43, 5, 1) == 0u);   // PLL_VCORST: VCO running
    CHECK(Wire.field(0, 0x40, 2, 1) == 1u);   // PLL_IS
}

TEST_CASE("the DAC channel enables survive a preset load")
{
    // DAC_RGBS_R0ENZ/G0ENZ/B0ENZ, s0_44[2], s0_44[5], s0_45[0], all 1. A
    // cleared B0ENZ is CLAUDE.md's yellow-tint fault, and its only runtime
    // writers are the two bypass switches, so on the scaling path nothing wrote
    // it and it survived as whatever the last table left.
    runBringUp();

    CHECK(Wire.field(0, 0x44, 2, 1) == 1u);
    CHECK(Wire.field(0, 0x44, 5, 1) == 1u);
    CHECK(Wire.field(0, 0x45, 0, 1) == 1u);
}

TEST_CASE("block resets are released after nothing that configures those blocks")
{
    // The six SFTRST_*_RSTZ fields are written 1, RELEASING their blocks, and a
    // reset released after its block is configured discards the configuration --
    // so they precede every s1/s3/s4/s5 write. Invisible in any per-class view,
    // since each class is internally in address order.
    std::vector<Write> writes = runBringUp();

    size_t lastReset = 0;
    size_t firstBlockConfig = writes.size();
    for (size_t i = 0; i < writes.size(); ++i) {
        if (writes[i].segment == 0 &&
            (writes[i].reg == 0x46 || writes[i].reg == 0x47))
            lastReset = i;
        if (writes[i].segment != 0 && i < firstBlockConfig)
            firstBlockConfig = i;
    }

    CHECK(lastReset < firstBlockConfig);
}

TEST_CASE("the sync processor's retiming auto-polarity is owned")
{
    // SP_HS_POL_ATO and SP_VS_POL_ATO, s5_55 bits 4 and 6. Without an owner
    // here, only the separate-sync branch and bypassModeSwitch_RGBHV() write
    // them, so a csync source leaves the retiming module auto-correcting
    // polarity from whatever the last mode left. The separate-sync branch still
    // lands afterwards, since BringUp::init() is doPostPresetLoadSteps()' first
    // statement.
    //
    // Three other s5_55 fields have owners, so the byte is written and still
    // wrong -- the case only a per-field check catches.
    CHECK(written(5, 0x55, 4, 1) == 0u);
    CHECK(written(5, 0x55, 6, 1) == 0u);
}

TEST_CASE("white level expansion is bypassed, and its gain is not the scanlines'")
{
    // VDS_W_LEV_BYPS s3_56[7] and VDS_WLEV_GAIN s3_58, whose only other writers
    // are the scanline toggles -- so a unit that never had scanlines on ran the
    // white level expansion on whatever gain survived from before. The gain is
    // inert while bypassed but is written anyway: a pair left disagreeing traps
    // whoever switches it on next.
    CHECK(written(3, 0x56, 7, 1) == 1u);
    CHECK(written(3, 0x58, 0, 8) == 26u);
}

TEST_CASE("the Y delay is the progressive one, not the HD one")
{
    // VDS_Y_DELAY s3_24[5:4], 2 in eleven tables. Nothing else writes 2 --
    // doPostPresetLoadSteps() only writes 3, for YPbPr and standards 3-9 -- so
    // on a 15 kHz RGB source the field was the previous mode's. The HD branches
    // still override this afterwards.
    CHECK(written(3, 0x24, 4, 2) == 2u);
}

TEST_CASE("the sync outputs come from the scaler, not from HD bypass")
{
    // OUT_SYNC_SEL s0_4f[7:6] = 0: H/V sync out of vds_proc, the only source the
    // scaling path has. Every other writer is a bypass path, so without this the
    // sync outputs stayed wherever the last bypass excursion left them -- which
    // is what "every register perfect, no HDMI" looks like. The bypass branch
    // runs later in the same function and still wins.
    CHECK(written(0, 0x4F, 6, 2) == 0u);
}

TEST_CASE("the OSD command handshake rests where the OSD leaves it")
{
    // OSD_COMMAND_FINISH s0_93[7] = 1 in all twelve tables. Every writer is in
    // osd.h and OSDManager.h, toggling it 0 and back to 1 around a command, so
    // the resting state is 1 and nothing establishes it until the OSD is first
    // drawn.
    CHECK(written(0, 0x93, 7, 1) == 1u);
}

TEST_CASE("the ADC PLL's charge pump and VCO gain are the scaling values")
{
    // PLLAD_ICP s5_17[2:0] = 6 and PLLAD_FS s5_11[5] = 1, the ADC PLL's charge
    // pump current and VCO gain. Every other writer on a 15 kHz RGB source is a
    // bypass path, so without these the PLL ran on whatever loop current the last
    // bypass excursion chose -- which is what a working dev unit reading ICP 5
    // with FS 1 is. Neither takes effect until PLLAD_LAT sees a rising edge, and
    // resetPLLAD() supplies one well after BringUp::init().
    CHECK(written(5, 0x17, 0, 3) == 6u);
    CHECK(written(5, 0x11, 5, 1) == 1u);
}

TEST_CASE("the input formatter's horizontal path is owned on a 15 kHz RGB source")
{
    // Six fields doPostPresetLoadSteps() writes only inside branches a 15 kHz
    // RGB source into a non-custom preset does not take -- YPbPr, standards
    // 3/4/8/9, and presetID 0x06/0x16 -- so nothing wrote them once the tables
    // went. Every value is what all ten SCALING tables ship; the two *_downscale
    // tables disagree on four of the six because they are the scale-down path
    // rather than a second opinion about this one.
    CHECK(written(1, 0x02, 4, 1) == 0u);   // IF_HS_TAP11_BYPS
    CHECK(written(1, 0x02, 5, 2) == 3u);   // IF_HS_Y_PDELAY
    CHECK(written(1, 0x0B, 4, 2) == 1u);   // IF_HS_DEC_FACTOR
    CHECK(written(1, 0x10, 0, 11) == 2u);  // IF_HB_ST,  blanking set 0
    CHECK(written(1, 0x12, 0, 11) == 72u); // IF_HB_SP,  blanking set 0
    CHECK(written(1, 0x26, 0, 12) == 272u);// IF_HBIN_SP
}

TEST_CASE("blanking set 1 is left alone, because it was measured inert")
{
    // IF_HB_ST1/SP1, s1_14 and s1_16: blanking set 1, measured inert. Written to
    // a zero-width window on a live unit the picture did not move and the
    // hardware suite passed 318/0, and in normal operation it disagrees with set
    // 2 permanently while the picture stays perfect
    // (docs/investigations/preset-abandonment-audit.md). So this asserts an
    // ABSENCE -- the only one in this file -- because without it the next pass
    // through the gap report will helpfully add the field back.
    CHECK(written(1, 0x14, 0, 11) == NotWritten);
    CHECK(written(1, 0x16, 0, 11) == NotWritten);
}

TEST_CASE("holding the blocks arms the bring-up, and running it disarms")
{
    // A held block loses its configuration, so whatever holds them is what says
    // the chip needs bringing up again. Nothing else may decide that: a preset
    // load no longer exists, and the mode-change path must not repeat a
    // bring-up the boot already did.
    Wire.reset();
    Wire.poison(Poison);

    Tv5725::BringUp::init();
    CHECK(Tv5725::BringUp::armed() == false);

    Tv5725::BringUp::holdAllBlocks();
    CHECK(Tv5725::BringUp::armed() == true);

    Tv5725::BringUp::init();
    CHECK(Tv5725::BringUp::armed() == false);
}

TEST_CASE("arming is a verb the bypass switches can use")
{
    // holdAllBlocks() arms because a held block loses its configuration. Bypass
    // arms for a different reason and needs to say so without pretending to
    // hold anything: it reconfigures the chip away from the scaling setup --
    // the input pads, both PLLs, the memory pad clock, the HD bypass reset --
    // and nothing on the scaling path claims those back.
    Wire.reset();
    Wire.poison(Poison);

    Tv5725::BringUp::init();
    CHECK(Tv5725::BringUp::armed() == false);

    Tv5725::BringUp::arm();
    CHECK(Tv5725::BringUp::armed() == true);
}

TEST_CASE("the ADC's automatic offset correction is the bring-up's")
{
    // Five registers the preset load wrote on every pass, with no other writer
    // anywhere -- constants being re-applied rather than a decision being made.
    // ADC_AUTO_OFST_V_RANGE already lived here; the rest of the family now does.
    CHECK(WRITTEN(Tv5725::Adc::ADC_AUTO_OFST_PRD) == 1);
    CHECK(WRITTEN(Tv5725::Adc::ADC_AUTO_OFST_DELAY) == 0);
    CHECK(WRITTEN(Tv5725::Adc::ADC_AUTO_OFST_STEP) == 0);
    CHECK(WRITTEN(Tv5725::Adc::ADC_AUTO_OFST_TEST) == 1);
    CHECK(WRITTEN(Tv5725::Adc::ADC_AUTO_OFST_RANGE_REG) == 0);
}

TEST_CASE("the peaking filter's shape is the bring-up's, its gain is not")
{
    // The cores and the band selects are constants with one writer. The GAINS
    // are excluded deliberately: VDS_PK_LB_GAIN and VDS_PK_LH_GAIN have seven
    // writers each, because the scanlines and peaking controls drive them, so a
    // bring-up value would be overwritten by whichever ran last.
    CHECK(WRITTEN(Tv5725::VideoProcessor::VDS_PK_LB_CORE) == 0);
    CHECK(WRITTEN(Tv5725::VideoProcessor::VDS_PK_LH_CORE) == 0);
    CHECK(WRITTEN(Tv5725::VideoProcessor::VDS_PK_VL_HL_SEL) == 0);
    CHECK(WRITTEN(Tv5725::VideoProcessor::VDS_PK_VL_HH_SEL) == 0);
    CHECK(WRITTEN(Tv5725::VideoProcessor::VDS_STEP_GAIN) == 1);

    CHECK(WRITTEN(Tv5725::VideoProcessor::VDS_PK_LB_GAIN) == NotWritten);
    CHECK(WRITTEN(Tv5725::VideoProcessor::VDS_PK_LH_GAIN) == NotWritten);
}

TEST_CASE("the input formatter's fixed horizontal filtering is the bring-up's")
{
    // Three constants with one writer each. Two of their neighbours in the same
    // block are deliberately NOT here:
    //
    //   IF_HS_SEL_LPF has a second writer later in the same load, which sets 0
    //   for one class of source. Written at bring-up instead, a source that
    //   took that branch would leave 0 behind for the next one.
    //
    //   IF_INI_ST has four writers, two of which set 16 from the sync watcher.
    CHECK(WRITTEN(Tv5725::InputFormatter::IF_HS_INT_LPF_BYPS) == 0);
    CHECK(WRITTEN(Tv5725::InputFormatter::IF_HS_PSHIFT_BYPS) == 1);
    CHECK(WRITTEN(Tv5725::InputFormatter::IF_LD_WRST_SEL) == 1);

    CHECK(WRITTEN(Tv5725::InputFormatter::IF_HS_SEL_LPF) == NotWritten);
    CHECK(WRITTEN(Tv5725::InputFormatter::IF_INI_ST) == NotWritten);
}

TEST_CASE("the sync processor's retime window starts where it always starts")
{
    // SP_RT_HS_ST is 0 whatever the divider is, in every path. Its partner
    // SP_RT_HS_SP is 93% of PLLAD_MD and belongs to SourceMeasurement, which is
    // why only one of the pair is here.
    CHECK(WRITTEN(Tv5725::SyncProcessor::SP_RT_HS_ST) == 0);
}
