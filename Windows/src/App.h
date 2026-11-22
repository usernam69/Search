// What every window shares: the theme, the windows, and the settings the
// Settings page reads and writes.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "Theme.h"

class BrowserWindow;

struct Engine {
    std::wstring id;
    std::wstring title;
    std::wstring url;  // %s where the words go
};

const std::vector<Engine>& engines();
const Engine& currentEngine();
void setEngine(const std::wstring& id);
std::wstring customEngineTemplate();
void setCustomEngineTemplate(const std::wstring& text);

namespace app {

// Called once at start-up, after GDI+ is up.
void init();

// The system's scale and light/dark, read again when either changes.
void refreshTheme();

std::vector<BrowserWindow*>& windows();
BrowserWindow* frontWindow();
void addWindow(BrowserWindow* window);
void removeWindow(BrowserWindow* window);

// The window the user is in, or the newest one.
BrowserWindow* anyWindow();

// Quit: every window's session is written, then the app closes.
void quit(bool askFirst = true);

// Settings.
std::wstring engineID();
void setEngineID(const std::wstring& id);
std::wstring customEngineTemplate();
void setCustomEngineTemplate(const std::wstring& text);
bool shieldOn();
void setShieldOn(bool on);
bool roomyTabs();
void setRoomyTabs(bool on);
std::wstring appearance();  // "system", "light", "dark"
void setAppearance(const std::wstring& look);
bool confirmQuit();
void setConfirmQuit(bool on);

// The address `typed` means: a place, a file on this PC, or words to search.
std::wstring resolve(const std::wstring& typed);

// The engine's address for `words`.
std::wstring searchURL(const std::wstring& words);

// Whether a URL is one of the app's own pages.
bool isInternal(const std::wstring& url);

// The internal page for a path, as "search://settings" or "about:history".
constexpr const wchar_t* kInternalPrefix = L"https://search.internal/";

// Opens an internal page in the front window.
void openInternal(const std::wstring& page);

}  // namespace app
