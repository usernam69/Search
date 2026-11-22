#include "App.h"

#include <shlobj.h>
#include <wrl/event.h>

#include <algorithm>

#include "Address.h"
#include "Data.h"
#include "Internal.h"
#include "Json.h"
#include "Shield.h"
#include "Store.h"
#include "Window.h"

#pragma comment(lib, "shlwapi.lib")

using namespace Microsoft::WRL;

// The engines, with the same templates as Engine.swift.
static const std::vector<Engine> kEngines = {
    {L"google", L"Google", L"https://www.google.com/search?q=%s"},
    {L"duckduckgo", L"DuckDuckGo", L"https://duckduckgo.com/?q=%s"},
    {L"bing", L"Bing", L"https://www.bing.com/search?q=%s"},
    {L"ecosia", L"Ecosia", L"https://www.ecosia.org/search?q=%s"},
    {L"startpage", L"Startpage", L"https://www.startpage.com/sp/search?query=%s"},
    {L"kagi", L"Kagi", L"https://kagi.com/search?q=%s"},
    {L"brave", L"Brave Search", L"https://search.brave.com/search?q=%s"},
    {L"qwant", L"Qwant", L"https://www.qwant.com/?q=%s"},
    {L"custom", L"Custom", L""},
};

const std::vector<Engine>& engines() { return kEngines; }

const Engine& currentEngine() {
    std::wstring id = app::engineID();
    for (auto& e : kEngines) {
        if (e.id == id) return e;
    }
    return kEngines[0];
}

// ---- the app ----

namespace app {

ComPtr<ICoreWebView2Environment> g_environment;
static std::vector<BrowserWindow*> g_windows;
static bool g_quitting = false;

void init() {
    // The theme's scale comes from the window that asked, so it is built
    // there; this only makes sure the settings that have defaults do.
    shield::enabled();
}

void refreshTheme() {}

std::vector<BrowserWindow*>& windows() { return g_windows; }

BrowserWindow* frontWindow() {
    HWND front = GetForegroundWindow();
    for (auto* w : g_windows) {
        if (w->hwnd() == front) return w;
    }
    return nullptr;
}

BrowserWindow* anyWindow() {
    if (auto* front = frontWindow()) return front;
    if (!g_windows.empty()) return g_windows.back();
    return nullptr;
}

void addWindow(BrowserWindow* window) { g_windows.push_back(window); }

void removeWindow(BrowserWindow* window) {
    g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), window), g_windows.end());
    if (g_windows.empty() && !g_quitting) {
        PostQuitMessage(0);
    }
}

void quit(bool askFirst) {
    if (askFirst && confirmQuit()) {
        int answer = MessageBoxW(anyWindow() ? anyWindow()->hwnd() : nullptr,
                                 L"Close Search and its tabs?", L"Search",
                                 MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (answer != IDOK) return;
    }
    g_quitting = true;
    auto list = g_windows;
    for (auto* w : list) w->close();
    PostQuitMessage(0);
}

// ---- settings ----

bool shieldOn() { return shield::enabled(); }
void setShieldOn(bool on) {
    shield::setEnabled(on);
    for (auto* w : g_windows) w->reloadShield();
}

bool roomyTabs() { return store::settingBool(L"sidebar.roomy", false); }
void setRoomyTabs(bool on) {
    store::setSetting(L"sidebar.roomy", on);
    for (auto* w : g_windows) w->relayout();
}

std::wstring appearance() { return store::setting(L"appearance").value_or(L"system"); }
void setAppearance(const std::wstring& look) {
    store::setSetting(L"appearance", look);
    for (auto* w : g_windows) w->applyThemeNow();
}

bool confirmQuit() { return store::settingBool(L"general.confirmQuit", false); }
void setConfirmQuit(bool on) { store::setSetting(L"general.confirmQuit", on); }



std::wstring engineID() { return store::setting(L"search.engine").value_or(L"google"); }
void setEngineID(const std::wstring& id) { store::setSetting(L"search.engine", id); }
std::wstring customEngineTemplate() { return store::setting(L"search.custom").value_or(L""); }
void setCustomEngineTemplate(const std::wstring& text) { store::setSetting(L"search.custom", text); }

// ---- the field's one job ----

static std::wstring percentEncode(const std::wstring& text) {
    // Only what a query can't carry: the Mac's unreserved set.
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring out;
    for (wchar_t c : text) {
        bool plain = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') ||
                     c == L'-' || c == L'.' || c == L'_' || c == L'~';
        if (plain) {
            out += c;
        } else {
            // UTF-8 bytes, then %XX each.
            char buf[8] = {};
            int n = WideCharToMultiByte(CP_UTF8, 0, &c, 1, buf, 8, nullptr, nullptr);
            for (int i = 0; i < n; ++i) {
                unsigned char b = (unsigned char)buf[i];
                out += L'%';
                out += hex[b >> 4];
                out += hex[b & 0xF];
            }
        }
    }
    return out;
}

std::wstring searchURL(const std::wstring& words) {
    const Engine& engine = currentEngine();
    std::wstring tpl = engine.url;
    if (engine.id == L"custom") {
        tpl = customEngineTemplate();
        if (tpl.empty() || tpl.find(L"%s") == std::wstring::npos) tpl = kEngines[0].url;
        if (tpl.find(L"://") == std::wstring::npos) tpl = L"https://" + tpl;
    }
    std::wstring encoded = percentEncode(words);
    size_t at = tpl.find(L"%s");
    if (at == std::wstring::npos) return tpl;
    return tpl.substr(0, at) + encoded + tpl.substr(at + 2);
}

std::wstring resolve(const std::wstring& typed) {
    std::wstring text = address::trim(typed);
    if (text.empty()) return L"";

    // The app's own pages, by name, as on the Mac.
    std::wstring low = address::lower(text);
    if (low == L"settings" || low == L"search://settings") return internalURL(L"settings");
    if (low == L"history" || low == L"search://history") return internalURL(L"history");
    if (low == L"bookmarks" || low == L"search://bookmarks") return internalURL(L"bookmarks");
    if (low == L"downloads" || low == L"search://downloads") return internalURL(L"downloads");
    if (low.rfind(L"search://", 0) == 0) return text;

    // A file on this PC.
    if (auto path = address::fileFrom(text)) return address::fileURL(*path);

    // A place, by Address.swift's rules.
    if (auto url = address::url(text)) {
        if (auto fixed = address::reachable(*url)) return *fixed;
        return *url;
    }

    // Anything else is words, and words ask the engine.
    return searchURL(text);
}

bool isInternal(const std::wstring& url) {
    return url.rfind(L"search://", 0) == 0 || url.rfind(L"search.internal/", 0) == 0;
}

void openInternal(const std::wstring& page) {
    if (auto* w = anyWindow()) w->openInternalPage(page);
}

}  // namespace app
