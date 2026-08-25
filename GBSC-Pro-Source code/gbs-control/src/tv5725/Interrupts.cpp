#include "Interrupts.h"

namespace Tv5725 {

bool Interrupts::takeSourceDisturbed()
{
    const bool disturbed = STATUS_INT_SOG_BAD::read() == 1 ||
                           STATUS_INT_SOG_SW::read() == 1;
    if (!disturbed)
        return false;

    INT_CONTROL_RST_SOGBAD::write(1);
    INT_CONTROL_RST_SOGSWITCH::write(1);
    INT_CONTROL_RST_SOGBAD::write(0);
    INT_CONTROL_RST_SOGSWITCH::write(0);
    return true;
}

void Interrupts::init()
{
    INT_RST_2::write(0x0);                       // s0_58[2:2]
    INT_RST_3::write(0x0);                       // s0_58[3:3]
    INT_RST_5::write(0x0);                       // s0_58[5:5]
    INT_RST_6::write(0x0);                       // s0_58[6:6]
    INT_RST_7::write(0x0);                       // s0_58[7:7]
    INT_ENABLE0::write(0x1);                     // s0_59[0:0]
    INT_ENABLE1::write(0x1);                     // s0_59[1:1]
    INT_ENABLE2::write(0x1);                     // s0_59[2:2]
    INT_ENABLE3::write(0x1);                     // s0_59[3:3]
    INT_ENABLE4::write(0x1);                     // s0_59[4:4]
    INT_ENABLE5::write(0x1);                     // s0_59[5:5]
    INT_ENABLE6::write(0x1);                     // s0_59[6:6]
    INT_ENABLE7::write(0x1);                     // s0_59[7:7]
}

}  // namespace Tv5725
