// A host-side stand-in for the ESP8266's LittleFS, backed by an in-memory map
// of path to contents.
//
// It stores bytes and answers reads rather than recording calls, so WHAT a file
// ended up holding is observable. A save that writes a partial file is the fault
// it exists to pin, and no assertion about call sequences can see it -- the
// format's own integrity check is its last line, so a test has to be able to
// read back what was actually written.
//
// The slow-flash boot is modelled too: readable() releases a file after N
// opens, which is what a flash awake enough to serve metadata and not yet awake
// enough to serve content does.

#ifndef FAKE_LITTLEFS_H_
#define FAKE_LITTLEFS_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <map>
#include <string>

class FakeFS;

class File {
public:
    File() : fs_(NULL), path_(), cursor_(0), open_(false), writing_(false) {}
    File(FakeFS *fs, const std::string &path, bool writing);

    operator bool() const { return open_; }

    int available() const;
    int read();
    size_t print(const char *text);
    size_t print(char c);
    uint32_t size() const;
    bool truncate(uint32_t to);
    void seekToEnd();
    void close() { open_ = false; }

private:
    FakeFS *fs_;
    std::string path_;
    uint32_t cursor_;
    bool open_;
    bool writing_;
};

class FakeFS {
public:
    FakeFS() : opens_(0), readableAfter_(0) {}

    void reset()
    {
        file.clear();
        opens_ = 0;
        readableAfter_ = 0;
    }

    // Contents by path, so a test both seeds and inspects the filesystem
    // directly rather than through the code under test.
    std::map<std::string, std::string> file;

    // How many opens are served empty before the content arrives. 0 is a
    // healthy flash.
    void serveEmptyForFirst(unsigned opens) { readableAfter_ = opens; }

    unsigned opens() const { return opens_; }

    bool begin() { return true; }
    bool exists(const char *path) const { return file.count(path) != 0; }
    bool remove(const char *path) { return file.erase(path) != 0; }

    bool rename(const char *from, const char *to)
    {
        if (!exists(from))
            return false;
        file[to] = file[from];
        file.erase(from);
        return true;
    }

    File open(const char *path, const char *mode)
    {
        ++opens_;
        const bool writing = mode != NULL && (mode[0] == 'w' || mode[0] == 'a');
        if (writing) {
            if (!exists(path))
                file[path] = std::string();
            File f(this, path, true);
            if (mode[0] == 'a')
                f.seekToEnd();
            return f;
        }
        if (!exists(path))
            return File();
        return File(this, path, false);
    }

    // Whether this open should see the content yet, which is what models a
    // flash still waking up.
    bool contentReadable() const { return opens_ > readableAfter_; }

private:
    unsigned opens_;
    unsigned readableAfter_;
};

extern FakeFS LittleFS;

inline File::File(FakeFS *fs, const std::string &path, bool writing)
    : fs_(fs), path_(path), cursor_(0), open_(true), writing_(writing)
{
}

inline int File::available() const
{
    if (!open_ || writing_ || !fs_->contentReadable())
        return 0;
    const std::string &held = fs_->file[path_];
    return cursor_ < held.size() ? (int)(held.size() - cursor_) : 0;
}

inline int File::read()
{
    if (available() <= 0)
        return -1;
    return (unsigned char)fs_->file[path_][cursor_++];
}

inline size_t File::print(const char *text)
{
    if (!open_ || !writing_ || text == NULL)
        return 0;
    const size_t length = strlen(text);
    std::string &held = fs_->file[path_];
    if (cursor_ + length > held.size())
        held.resize(cursor_ + length);
    held.replace(cursor_, length, text);
    cursor_ += length;
    return length;
}

inline size_t File::print(char c)
{
    const char text[2] = {c, '\0'};
    return print(text);
}

inline void File::seekToEnd()
{
    if (open_)
        cursor_ = (uint32_t)fs_->file[path_].size();
}

inline bool File::truncate(uint32_t to)
{
    if (!open_)
        return false;
    std::string &held = fs_->file[path_];
    if (to < held.size())
        held.resize(to);
    return true;
}

inline uint32_t File::size() const
{
    if (!open_)
        return 0;
    return (uint32_t)fs_->file[path_].size();
}

#endif  // FAKE_LITTLEFS_H_
