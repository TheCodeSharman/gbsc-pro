#include "FramingFile.h"

#include <Arduino.h>   // F(), which keeps the header literal in flash
#include <LittleFS.h>

#include "../tv5725/FramingTable.h"
#include "../tv5725/FramingText.h"

namespace Prefs {

const uint8_t FramingFile::LineBytes;

FramingFile::FramingFile(Tv5725::FramingTable &table, const char *path)
    : table_(table), path_(path), readable_(false)
{
}

bool FramingFile::readable() const { return readable_; }

bool FramingFile::read()
{
    readable_ = false;

    if (!LittleFS.exists(path_)) {
        readable_ = true;
        return true;
    }

    File f = LittleFS.open(path_, "r");
    if (!f)
        return false;

    const uint32_t claimed = f.size();
    uint32_t consumed = 0;

    Tv5725::FramingTable arriving;
    Tv5725::FramingText text(arriving);
    char line[LineBytes];
    uint8_t held = 0;
    while (f.available() > 0) {
        const int c = f.read();
        if (c < 0)
            break;
        ++consumed;
        if (c != '\n' && c != '\r') {
            if (held < sizeof(line) - 1)
                line[held++] = (char)c;
            continue;
        }
        if (held == 0)
            continue;
        line[held] = '\0';
        held = 0;
        text.readLine(line);
    }

    // A last line with no newline after it is still a line.
    if (held != 0) {
        line[held] = '\0';
        text.readLine(line);
    }

    f.close();

    if (consumed < claimed)
        return false;

    table_.adopt(arriving);
    readable_ = true;
    return true;
}

bool FramingFile::write()
{
    if (!readable_)
        return false;

    File f = LittleFS.open(path_, "w");
    if (!f)
        return false;

    // "w" alone has been measured leaving a tail from whatever wrote the file
    // longer, and a framing line read back out of that tail is a tuning the
    // user never made. ../../../../docs/known-issues.md
    f.truncate(0);

    f.print(F("# framing, one source a line: "
              "<lines>@<fieldRateHz>/<syncWidth><hPol><vPol> = "
              "originH extentH originV extentV shape\n"
              "# in ten-thousandths of the capturable region; shape 0 fills\n"));

    Tv5725::FramingText text(table_);
    char line[LineBytes];
    for (uint16_t index = 0; index < table_.count(); ++index)
        if (text.writeLine(index, line, sizeof(line))) {
            f.print(line);
            f.print('\n');
        }

    f.close();
    return true;
}

}  // namespace Prefs
