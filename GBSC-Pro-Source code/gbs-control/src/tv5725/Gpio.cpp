#include "Gpio.h"

#include "../../gbs_types.h"

namespace Tv5725 {

void Gpio::init()
{
    GBS::GPIO_SEL_0::write(0x1);                      // s0_52[0:0]
    GBS::GPIO_SEL_1::write(0x1);                      // s0_52[1:1]
    GBS::GPIO_SEL_2::write(0x1);                      // s0_52[2:2]
    GBS::GPIO_SEL_3::write(0x0);                      // s0_52[3:3]
    GBS::GPIO_SEL_4::write(0x0);                      // s0_52[4:4]
    GBS::GPIO_SEL_5::write(0x1);                      // s0_52[5:5]
    GBS::GPIO_SEL_6::write(0x1);                      // s0_52[6:6]
    GBS::GPIO_SEL_7::write(0x0);                      // s0_52[7:7]
    GBS::GPIO_EN_0::write(0x0);                       // s0_53[0:0]
    GBS::GPIO_EN_1::write(0x0);                       // s0_53[1:1]
    GBS::GPIO_EN_2::write(0x0);                       // s0_53[2:2]
    GBS::GPIO_EN_3::write(0x0);                       // s0_53[3:3]
    GBS::GPIO_EN_4::write(0x0);                       // s0_53[4:4]
    GBS::GPIO_EN_5::write(0x0);                       // s0_53[5:5]
    GBS::GPIO_EN_6::write(0x0);                       // s0_53[6:6]
    GBS::GPIO_EN_7::write(0x0);                       // s0_53[7:7]
    GBS::GPIO_VAL_0::write(0x0);                      // s0_54[0:0]
    GBS::GPIO_VAL_1::write(0x0);                      // s0_54[1:1]
    GBS::GPIO_VAL_2::write(0x0);                      // s0_54[2:2]
    GBS::GPIO_VAL_3::write(0x0);                      // s0_54[3:3]
    GBS::GPIO_VAL_4::write(0x0);                      // s0_54[4:4]
    GBS::GPIO_VAL_5::write(0x0);                      // s0_54[5:5]
    GBS::GPIO_VAL_6::write(0x0);                      // s0_54[6:6]
    GBS::GPIO_VAL_7::write(0x0);                      // s0_54[7:7]
}

}  // namespace Tv5725
