#ifndef TV5725_GPIO_H
#define TV5725_GPIO_H

#include "Tv5725.h"

namespace Tv5725 {

// The general-purpose IO mux: what each of the eight pins carries, which way it
// faces, and what it drives.
//
// GPIO_SEL_n chooses the pin's GPIO function over its alternate, GPIO_EN_n is
// the output enable and GPIO_VAL_n the level. Every enable is 0, so all eight
// are inputs and GPIO_VAL_n is inert. Nothing on this board reads them back --
// the schematic gives the TV5725's GPIO no net worth naming, and input routing
// is the HC32F460's -- so these are the tables' values, kept for continuity.
class Gpio {
public:
    typedef UReg<0x00, 0x52, 0, 8> GPIO_CONTROL_00;

    typedef UReg<0x00, 0x52, 0, 1> GPIO_SEL_0;                        // GPIO bit0 selection When = 0, GPIO (pin76) is used as
                                                                      // INTZ output

    typedef UReg<0x00, 0x52, 1, 1> GPIO_SEL_1;                        // When = 1, GPIO (pin76) is used as GPIO bit0 GPIO bit1
                                                                      // selection When = 0, HALF (pin77) is used as half tone
                                                                      // input When = 1, HALF (pin77) is used as GPIO bit1

    typedef UReg<0x00, 0x52, 2, 1> GPIO_SEL_2;                        // GPIO bit2 selection When = 0, SCLSA (pin43) is used as
                                                                      // two wire serial bus slave address selection

    typedef UReg<0x00, 0x52, 3, 1> GPIO_SEL_3;                        // When = 1, SCLSA (pin43) is used as GPIO bit2 GPIO bit3
                                                                      // selection When = 0, MBA (pin107) is used as external
                                                                      // memory BA When = 1, MBA (pin107) is used as GPIO bit3

    typedef UReg<0x00, 0x52, 4, 1> GPIO_SEL_4;                        // GPIO bit4 selection When = 0, MCS1 (pin109) is used as
                                                                      // external memory CS1

    typedef UReg<0x00, 0x52, 5, 1> GPIO_SEL_5;                        // When = 1, MCS1 (pin109) is used as GPIO bit4 GPIO bit5
                                                                      // selection When = 0, HBOUT (pin6) is used as H-blank
                                                                      // output When = 1, HBOUT (pin6) is used as GPIO bit5

    typedef UReg<0x00, 0x52, 6, 1> GPIO_SEL_6;                        // GPIO bit6 selection When = 0, VBOUT (pin7) is used as
                                                                      // V-blank output

    typedef UReg<0x00, 0x52, 7, 1> GPIO_SEL_7;                        // When = 1, VBOUT (pin7) is used as GPIO bit6 GPIO bit7
                                                                      // selection When = 0, CLKOUT (pin4) is used as clock output
                                                                      // When = 1, CLKOUT (pin4) is used as GPIO bit7

    typedef UReg<0x00, 0x53, 0, 8> GPIO_CONTROL_01;

    typedef UReg<0x00, 0x53, 0, 1> GPIO_EN_0;                         // GPIO bit0 output enable When = 0, GPIO bit0 output
                                                                      // disable

    typedef UReg<0x00, 0x53, 1, 1> GPIO_EN_1;                         // When = 1, GPIO bit0 output enable GPIO bit1 output enable
                                                                      // When = 0, GPIO bit1 output disable When = 1, GPIO bit1
                                                                      // output enable

    typedef UReg<0x00, 0x53, 2, 1> GPIO_EN_2;                         // GPIO bit2 output enable When = 0, GPIO bit2 output
                                                                      // disable

    typedef UReg<0x00, 0x53, 3, 1> GPIO_EN_3;                         // When = 1, GPIO bit2 output enable GPIO bit3 output enable
                                                                      // When = 0, GPIO bit3 output disable When = 1, GPIO bit3
                                                                      // output enable

    typedef UReg<0x00, 0x53, 4, 1> GPIO_EN_4;                         // GPIO bit4 output enable When = 0, GPIO bit4 output
                                                                      // disable

    typedef UReg<0x00, 0x53, 5, 1> GPIO_EN_5;                         // When = 1, GPIO bit4 output enable GPIO bit5 output enable
                                                                      // When = 0, GPIO bit5 output disable When = 1, GPIO bit5
                                                                      // output enable

    typedef UReg<0x00, 0x53, 6, 1> GPIO_EN_6;                         // GPIO bit6 output enable When = 0, GPIO bit6 output
                                                                      // disable

    typedef UReg<0x00, 0x53, 7, 1> GPIO_EN_7;                         // When = 1, GPIO bit6 output enable GPIO bit7 output enable
                                                                      // When = 0, GPIO bit7 output disable When = 1, GPIO bit7
                                                                      // output enable

    typedef UReg<0x00, 0x54, 0, 1> GPIO_VAL_0;                        // GPIO bit0 output value

    typedef UReg<0x00, 0x54, 1, 1> GPIO_VAL_1;                        // GPIO bit1 output value

    typedef UReg<0x00, 0x54, 2, 1> GPIO_VAL_2;                        // GPIO bit2 output value

    typedef UReg<0x00, 0x54, 3, 1> GPIO_VAL_3;                        // GPIO bit3 output value

    typedef UReg<0x00, 0x54, 4, 1> GPIO_VAL_4;                        // GPIO bit4 output value

    typedef UReg<0x00, 0x54, 5, 1> GPIO_VAL_5;                        // GPIO bit5 output value

    typedef UReg<0x00, 0x54, 6, 1> GPIO_VAL_6;                        // GPIO bit6 output value

    typedef UReg<0x00, 0x54, 7, 1> GPIO_VAL_7;                        // GPIO bit7 output value

    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_GPIO_H
