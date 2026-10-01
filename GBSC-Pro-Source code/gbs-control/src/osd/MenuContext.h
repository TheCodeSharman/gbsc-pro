#ifndef OSD_MENU_CONTEXT_H_
#define OSD_MENU_CONTEXT_H_

// What an item reads to say what it is currently set to. The menu never mutates
// any of it: an Ok yields a letter and the sketch queues it as a user command,
// so one action per option serves the remote, the web and the serial console
// alike. docs/osd-menu.md

struct userOptions;
struct avOptions;

namespace Tv5725 {
class Controls;
}

namespace Osd {

class MenuContext {
public:
    MenuContext(Tv5725::Controls &controls, userOptions &options,
                avOptions &av);

    Tv5725::Controls &controls() const;
    userOptions &options() const;

    // The AV module's picture, which is not a scaler preference and is not in
    // userOptions for that reason.
    avOptions &av() const;

private:
    Tv5725::Controls &controls_;
    userOptions &options_;
    avOptions &av_;
};

}  // namespace Osd

#endif  // OSD_MENU_CONTEXT_H_
