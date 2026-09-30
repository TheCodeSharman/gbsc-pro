#ifndef OSD_MENU_COMMAND_H_
#define OSD_MENU_COMMAND_H_

// What a press asks the sketch to do: a letter and which of the board's two
// command surfaces it belongs to. Four letters mean different things on each --
// 'Z' toggles matched presets through /sc? and bumps VDS_Y_OFST through /uc? --
// so a letter alone does not say what was asked. docs/osd-menu.md

namespace Osd {

class MenuCommand {
public:
    // /uc? reaches handleType2Command(); /sc? reaches the switch in loop().
    enum Queue { UserCommand, SerialCommand };

    constexpr MenuCommand() : queue_(UserCommand), letter_(0) {}

    static constexpr MenuCommand user(char letter)
    {
        return MenuCommand(UserCommand, letter);
    }

    static constexpr MenuCommand serial(char letter)
    {
        return MenuCommand(SerialCommand, letter);
    }

    Queue queue() const;
    char letter() const;

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
