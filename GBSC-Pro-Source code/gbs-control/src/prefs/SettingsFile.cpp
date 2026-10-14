#include "SettingsFile.h"

#include <Arduino.h>   // delay(), a flash settling time
#include <LittleFS.h>

#include "Settings.h"

namespace Prefs {

const uint8_t SettingsFile::BootAttempts;
const uint16_t SettingsFile::RetryDelayMs;
const uint8_t SettingsFile::LineBytes;

SettingsFile::SettingsFile(Settings &settings, const char *path)
    : settings_(settings), path_(path)
{
}

bool SettingsFile::exists() const { return LittleFS.exists(path_); }

bool SettingsFile::readOnce(Attempt *attempt)
{
    attempt->opened = false;
    attempt->size = 0;
    attempt->applied = 0;
    attempt->reachedEnd = false;

    File f = LittleFS.open(path_, "r");
    if (!f)
        return false;

    attempt->opened = true;
    attempt->size = f.size();

    settings_.defaults();

    char line[LineBytes];
    uint8_t held = 0;
    while (f.available() > 0 && !attempt->reachedEnd) {
        const int c = f.read();
        if (c < 0)
            break;
        if (c != '\n' && c != '\r') {
            if (held < sizeof(line) - 1)
                line[held++] = (char)c;
            continue;
        }
        if (held == 0)
            continue;

        line[held] = '\0';
        held = 0;
        switch (settings_.readLine(line)) {
            case Settings::Applied:
                ++attempt->applied;
                break;
            case Settings::End:
                attempt->reachedEnd = true;
                break;
            default:
                break;
        }
    }

    // A last line with no newline after it is still a line.
    if (!attempt->reachedEnd && held != 0) {
        line[held] = '\0';
        switch (settings_.readLine(line)) {
            case Settings::Applied:
                ++attempt->applied;
                break;
            case Settings::End:
                attempt->reachedEnd = true;
                break;
            default:
                break;
        }
    }

    f.close();
    return attempt->reachedEnd;
}

bool SettingsFile::read(uint8_t attempts, Report report)
{
    for (uint8_t index = 0; index < attempts; ++index) {
        Attempt attempt;
        attempt.index = index;
        const bool done = readOnce(&attempt);

        if (report != NULL)
            report(attempt);
        if (done)
            return true;

        delay(RetryDelayMs);
    }

    return false;
}

bool SettingsFile::write()
{
    File f = LittleFS.open(path_, "w");
    if (!f)
        return false;

    // "w" alone has been measured leaving a tail from whatever wrote the file
    // longer. The terminator makes one inert to a read, but a settings file is
    // read by people as well as by this firmware. ../../../../docs/known-issues.md
    f.truncate(0);

    f.print(F("# GBSC-Pro settings, one key = value a line. A missing key takes\n"
              "# its default and an unknown one is ignored, so adding or removing\n"
              "# a setting cannot shift another. The last line is what says the\n"
              "# file was written whole. docs/preferences-file.md\n"));

    char line[LineBytes];
    for (uint16_t index = 0; settings_.writeLine(index, line, sizeof(line));
         ++index) {
        f.print(line);
        f.print('\n');
    }

    f.print(Settings::terminator());
    f.print('\n');
    f.close();
    return true;
}

}  // namespace Prefs
