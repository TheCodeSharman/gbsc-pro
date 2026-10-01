#ifndef OSD_MENU_COMMAND_H_
#define OSD_MENU_COMMAND_H_

// What a press asks the sketch to do, and which surface it asks on. Four letters
// mean different things on the two command surfaces -- 'Z' toggles matched
// presets through /sc? and bumps VDS_Y_OFST through /uc? -- so a letter alone
// does not say what was asked. Selecting a source is a surface of its own, being
// an id rather than a letter. docs/osd-menu.md

#include "../videosource/VideoSourceSelection.h"

namespace Osd {

class MenuCommand {
public:
    // /uc? reaches handleType2Command(), /sc? the switch in loop(), and an
    // input selection pendingInputSelection, which /input?src= also queues.
    enum Queue { UserCommand, SerialCommand, InputSelection };

    constexpr MenuCommand() : queue_(UserCommand), letter_(0) {}

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

    Queue queue() const;
    char letter() const;

    // The source an InputSelection names. None on any other surface, which is
    // also what a press asking for nothing yields.
    VideoSourceSelection::Id source() const;

    // False where the press asked for nothing, which is every navigation key.
    bool asked() const;

private:
    constexpr MenuCommand(Queue queue, char letter)
        : queue_(queue), letter_(letter)
    {
    }

    Queue queue_;
    char letter_;
};

}  // namespace Osd

#endif  // OSD_MENU_COMMAND_H_
