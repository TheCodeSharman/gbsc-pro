#ifndef TEST_LOGGED_LINES_H_
#define TEST_LOGGED_LINES_H_

// tv5725Log(), kept rather than discarded, so a suite can assert what the
// firmware said. Header-only and defining its global the way the other seams
// here do: every host test is a single-translation-unit binary.

#include <string>
#include <vector>

std::vector<std::string> g_logLines;

void tv5725Log(const char *line) { g_logLines.push_back(line); }

static bool loggedContaining(const char *text)
{
    for (size_t i = 0; i < g_logLines.size(); ++i)
        if (g_logLines[i].find(text) != std::string::npos)
            return true;
    return false;
}

#endif  // TEST_LOGGED_LINES_H_
