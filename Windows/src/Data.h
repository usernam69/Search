// Where you have been, what you kept, and what was open last time — the three
// small files the field finishes addresses from and the session comes back
// from. Their shapes are the Mac's, so a history file copied across reads.
#pragma once

#include <ctime>
#include <string>
#include <vector>

struct Visit {
    std::wstring url;
    std::wstring title;
    long long at = 0;      // seconds since the epoch
    int visits = 1;
};

struct Bookmark {
    std::wstring id;
    std::wstring title;
    std::wstring url;      // empty for a folder
    std::vector<Bookmark> children;

    bool isFolder() const { return url.empty(); }
};

// A tab as it comes back at launch.
struct SessionTab {
    std::wstring url;
    std::wstring title;
    std::wstring pin;      // the letter a pinned tab shows
    std::wstring name;     // the name you gave the tab, when you gave it one
    std::wstring home;     // a pin's own page, the one it was pinned at
    bool muted = false;
};

struct Session {
    std::vector<SessionTab> tabs;
    int active = 0;
};

namespace history {
std::vector<Visit> read();
void write(const std::vector<Visit>& visits);
// A visit is written a moment after it happens, as on the Mac, rather than on
// every keystroke. `note` merges one visit into the list it keeps in memory.
void note(const std::wstring& url, const std::wstring& title);
void flush();
void clear();
}  // namespace history

namespace bookmarks {
std::vector<Bookmark> read();
void write(const std::vector<Bookmark>& list);
}  // namespace bookmarks

namespace session {
Session read();
void write(const Session& shape);
// Kept beside the session while ⇧⌘T can still bring a closed tab back.
struct ClosedTab {
    std::wstring url;
    std::wstring title;
    int index = 0;
};
std::vector<ClosedTab> readClosed();
void writeClosed(const std::vector<ClosedTab>& closed);
}  // namespace session
