#ifndef OSD_MENU_COMMAND_H_
#define OSD_MENU_COMMAND_H_

// What a press asks the sketch to do, and which surface it asks on. Four letters
// mean different things on the two command surfaces -- 'Z' toggles matched
// presets through /sc? and bumps VDS_Y_OFST through /uc? -- so a letter alone
// does not say what was asked. Selecting a source is a surface of its own, being
// an id rather than a letter, and so are a geometry pad and an adjustable value.
// docs/osd-menu.md

#include "../tv5725/Nudge.h"
#include "../videosource/VideoSourceSelection.h"
#include "Tune.h"

namespace Osd {

class MenuCommand {
public:
    // /uc? reaches handleType2Command(), /sc? the switch in loop(), and an
    // input selection pendingInputSelection, which /input?src= also queues. A
    // nudge reaches Tv5725::Controls, which takes it in granules, and a tune
    // the class holding the value it names.
    enum Queue {
        UserCommand,
        SerialCommand,
        InputSelection,
        GeometryNudge,
        ValueTune
    };

    constexpr MenuCommand() : queue_(UserCommand), letter_(0), direction_(0) {}

    static constexpr MenuCommand user(char letter)
    {
        return MenuCommand(UserCommand, letter);
    }

    static constexpr MenuCommand serial(char letter)
    {
        return MenuCommand(SerialCommand, letter);
    }

    static constexpr MenuCommand input(VideoSourceSelection::Id source)
    {
        return MenuCommand(InputSelection, (char)source);
    }

    // One of the four geometry controls and the way it goes. How far is the
    // caller's: a remote multiplies a tap by its hold ramp.
    static constexpr MenuCommand nudge(Tv5725::Nudge::Control control,
                                       int8_t direction)
    {
        return MenuCommand(GeometryNudge, (char)control, direction);
    }

    // One held value and the way it goes. A row's Left and Right carry one
    // each, and the number of counts is the caller's.
    static constexpr MenuCommand tune(Tune::Control control, int8_t direction)
    {
        return MenuCommand(ValueTune, (char)control, direction);
    }

    Queue queue() const;
    char letter() const;

    // The source an InputSelection names. None on any other surface, which is
    // also what a press asking for nothing yields.
    VideoSourceSelection::Id source() const;

    // The control a GeometryNudge names, and +1 or -1 for the way it goes. The
    // direction is 0 on every other surface, which is what says there is none.
    Tv5725::Nudge::Control control() const;
    int8_t direction() const;

    // The value a ValueTune names. Meaningless on any other surface, as
    // control() is.
    Tune::Control tuned() const;

    // False where the press asked for nothing, which is every navigation key.
    bool asked() const;

private:
    constexpr MenuCommand(Queue queue, char letter, int8_t direction = 0)
        : queue_(queue), letter_(letter), direction_(direction)
    {
    }

    Queue queue_;
    char letter_;
    int8_t direction_;
};

}  // namespace Osd

#endif  // OSD_MENU_COMMAND_H_
