#include "Interrupts.h"

#include "../../gbs_types.h"

namespace Tv5725 {

void Interrupts::init()
{
    GBS::INT_RST_2::write(0x0);                       // s0_58[2:2]
    GBS::INT_RST_3::write(0x0);                       // s0_58[3:3]
    GBS::INT_RST_5::write(0x0);                       // s0_58[5:5]
    GBS::INT_RST_6::write(0x0);                       // s0_58[6:6]
    GBS::INT_RST_7::write(0x0);                       // s0_58[7:7]
    GBS::INT_ENABLE0::write(0x1);                     // s0_59[0:0]
    GBS::INT_ENABLE1::write(0x1);                     // s0_59[1:1]
    GBS::INT_ENABLE2::write(0x1);                     // s0_59[2:2]
    GBS::INT_ENABLE3::write(0x1);                     // s0_59[3:3]
    GBS::INT_ENABLE4::write(0x1);                     // s0_59[4:4]
    GBS::INT_ENABLE5::write(0x1);                     // s0_59[5:5]
    GBS::INT_ENABLE6::write(0x1);                     // s0_59[6:6]
    GBS::INT_ENABLE7::write(0x1);                     // s0_59[7:7]
}

}  // namespace Tv5725
