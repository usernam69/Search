// The embedded browser engine every tab's page runs in: CEF, shipped beside
// Search.exe with no dependency on Edge or a system WebView2 install.
//
// The rest of the app speaks only this small interface; all of CEF stays in
// WebView.cpp, including its process entry point and its external message
// pump. A tab with no engine state yet is one restored from the last session,
// waiting to be picked.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

struct Tab;

namespace engine {

// Called before anything else. If this is a CEF child process, it runs its
// own entry point and returns true with the code main() should exit with.
bool preMain(HINSTANCE instance, int* exitCode);

// Called once in the browser process.
bool initialize(HINSTANCE instance);
void shutdown();
void pump();
int exitCode();

// The profile CEF keeps beside the app's other files.
std::wstring userDataFolder();

// A string as a JavaScript literal, quotes and all.
std::wstring jsString(const std::wstring& text);

// A tab's browser: made when the tab is first shown, gone when it is closed.
bool create(Tab* tab, HWND parent, const std::wstring& url);
bool has(Tab* tab);
void destroy(Tab* tab);
void setVisible(Tab* tab, bool visible);
void setBounds(Tab* tab, const RECT& page, const std::vector<RECT>& caption, int radius);
void focus(Tab* tab);

void navigate(Tab* tab, const std::wstring& url);
void loadHTML(Tab* tab, const std::wstring& html, const std::wstring& url);
void goBack(Tab* tab);
void goForward(Tab* tab);
void reload(Tab* tab);
void stop(Tab* tab);
void setMuted(Tab* tab, bool muted);

}  // namespace engine
