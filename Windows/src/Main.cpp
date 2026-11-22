// The app: the window's theme, its title, and the door the app's own pages
// come back through. And the entry point.
#include <windows.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl.h>

#include <algorithm>
#include <set>
#include <string>

#include "Address.h"
#include "App.h"
#include "Data.h"
#include "Internal.h"
#include "Shield.h"
#include "Store.h"
#include "Theme.h"
#include "Window.h"

#pragma comment(lib, "dwmapi.lib")

using namespace Microsoft::WRL;

static std::set<BrowserWindow*> g_live;
std::set<BrowserWindow*>& liveWindows() { return g_live; }

// ---- the theme ----

void BrowserWindow::applyTheme() {
    BOOL dark = FALSE;
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) {
        dark = value == 0;
    }
    // Settings › Appearance: light, dark, or the system's.
    std::wstring look = app::appearance();
    if (look == L"light") dark = FALSE;
    if (look == L"dark") dark = TRUE;

    g_theme.build(scale_, dark != FALSE);

    BOOL useDark = dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    // The window's own border, so the resize edge is the same tone as the
    // column beside it.
    COLORREF border = g_theme.palette.frame.ref();
    DwmSetWindowAttribute(hwnd_, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    redraw();
}

void BrowserWindow::updateTitle() {
    Tab* tab = active();
    std::wstring title = tab ? tab->shownTitle() : L"Search";
    SetWindowTextW(hwnd_, (title + L" — Search").c_str());
}

// ---- the app's own pages' switches ----

void BrowserWindow::runInternalAction(const std::wstring& text) {
    std::wstring action = text;
    if (action.rfind(L"search://act/", 0) == 0) action = action.substr(13);
    std::wstring low = address::lower(action);

    if (low == L"shield") {
        app::setShieldOn(!shield::enabled());
    } else if (low == L"roomy") {
        app::setRoomyTabs(!app::roomyTabs());
    } else if (low == L"confirm") {
        app::setConfirmQuit(!app::confirmQuit());
    } else if (low.rfind(L"engine:", 0) == 0) {
        app::setEngineID(action.substr(7));
    } else if (low.rfind(L"custom:", 0) == 0) {
        app::setCustomEngineTemplate(action.substr(7));
    } else if (low.rfind(L"look:", 0) == 0) {
        app::setAppearance(low.substr(5));
    } else if (low.rfind(L"open:", 0) == 0) {
        std::wstring url = action.substr(5);
        navigate(url);
        return;
    } else if (low == L"clear-history") {
        history::clear();
    }

    // The page is redrawn so the switch it just flipped shows its new state.
    if (Tab* tab = active(); tab && tab->isInternal()) {
        if (tab->webview) tab->webview->NavigateToString(internalHTML(tab->internal).c_str());
    }
    relayout();
}

// ---- the entry point ----

static Gdiplus::GdiplusStartupInput g_startup;
static ULONG_PTR g_gdiplus = 0;

static void newWindowNow(bool restore) {
    auto* window = new BrowserWindow();
    if (!window->create(restore)) {
        wchar_t msg[256];
        swprintf(msg, 256, L"create failed, GetLastError=%lu", GetLastError());
        store::log(msg);
        delete window;
        return;
    }
    app::addWindow(window);
    window->showAndFocus();
}

void newWindow() { newWindowNow(false); }

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // One copy at a time: a second launch brings the first
    // forward rather than opening a second window.
    HANDLE one = CreateMutexW(nullptr, TRUE, L"Search.Browser.Single");
    if (one && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"SearchBrowserWindow", nullptr)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Gdiplus::GdiplusStartup(&g_gdiplus, &g_startup, nullptr);

    // The window's icon, from the resource the .rc puts in.
    HICON icon = (HICON)LoadImageW(instance, L"IDI_APP", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    (void)icon;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    app::init();

    // A link from another app, or the app itself with nothing to do.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring first;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg.rfind(L"http://", 0) == 0 || arg.rfind(L"https://", 0) == 0 || arg.rfind(L"file://", 0) == 0) {
            first = arg;
            break;
        }
    }
    if (argv) LocalFree(argv);

    newWindowNow(true);
    if (!first.empty()) {
        if (BrowserWindow* window = app::anyWindow()) window->navigate(first);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_gdiplus) Gdiplus::GdiplusShutdown(g_gdiplus);
    return 0;
}
