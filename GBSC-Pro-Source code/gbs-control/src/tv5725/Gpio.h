#ifndef TV5725_GPIO_H
#define TV5725_GPIO_H

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
    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_GPIO_H
