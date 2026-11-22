// The window itself: a column of tabs down the left, the page as a card, and
// the band across the top where the address field comes up. Windows' own
// caption is not drawn — the system's minimise, maximise and close are drawn
// at the top right, faint until the pointer comes near them, so the page keeps
// the whole window.
#include "Window.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wrl/event.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

#include "Address.h"
#include "App.h"
#include "Data.h"
#include "Internal.h"
#include "Shield.h"
#include "Store.h"
#include "WebView.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Microsoft::WRL;

static const wchar_t* kWindowClass = L"SearchBrowserWindow";

// Windows 11's rounded corners and border colour, which older SDKs don't name.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

static bool alive(BrowserWindow* window) { return liveWindows().count(window) > 0; }

// ---- Tab ----

std::wstring Tab::shownTitle() const {
    if (!name.empty()) return name;
    if (!title.empty()) return title;
    if (isInternal()) return internalTitle(internal);
    if (!url.empty()) return address::pretty(url);
    return L"New Tab";
}

std::wstring Tab::shownURL() const {
    if (isInternal()) return internalURL(internal);
    return url;
}

// ---- the window ----

static BrowserWindow* windowFor(HWND hwnd) { return (BrowserWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA); }

void BrowserWindow::registerClass() {
    static bool done = false;
    if (done) return;
    done = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM w, LPARAM l) -> LRESULT {
        if (message == WM_NCCREATE) {
            auto* cs = (CREATESTRUCTW*)l;
            auto* window = (BrowserWindow*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
            // The handle is only known to the message until CreateWindowExW
            // returns, so the window takes it here.
            window->setHwnd(hwnd);
        }
        if (BrowserWindow* window = windowFor(hwnd)) return window->handle(message, w, l);
        return DefWindowProcW(hwnd, message, w, l);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), L"IDI_APP", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(GetModuleHandleW(nullptr), L"IDI_APP", IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);
}

BrowserWindow::BrowserWindow() { liveWindows().insert(this); }

BrowserWindow::~BrowserWindow() {
    liveWindows().erase(this);
    for (auto& tab : tabs_) killWebView(tab.get());
    tabs_.clear();
}

bool BrowserWindow::create(bool restore) {
    registerClass();

    HDC screen = GetDC(nullptr);
    scale_ = GetDeviceCaps(screen, LOGPIXELSX) / 96.0;
    ReleaseDC(nullptr, screen);
    g_theme.build(scale_, g_theme.dark);

    RECT frame{0, 0, g_theme.px(1180), g_theme.px(780)};
    if (auto saved = store::setting(L"window.frame")) {
        RECT r{};
        if (swscanf_s(saved->c_str(), L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) == 4 &&
            r.right - r.left > 400 && r.bottom - r.top > 300) {
            frame = r;
        }
    }

    // The window opens inside the monitor's work area: a taskbar along the top
    // or the left would otherwise sit over the band, and over the field.
    MONITORINFO area{sizeof(area)};
    if (GetMonitorInfoW(MonitorFromRect(&frame, MONITOR_DEFAULTTONEAREST), &area)) {
        RECT& work = area.rcWork;
        LONG w = frame.right - frame.left, h = frame.bottom - frame.top;
        if (frame.left < work.left || frame.top < work.top || frame.right > work.right ||
            frame.bottom > work.bottom) {
            LONG cx = work.right - work.left - w, cy = work.bottom - work.top - h;
            frame.left = work.left + (cx > 0 ? cx / 2 : 0);
            frame.top = work.top + (cy > 0 ? cy / 2 : 0);
            frame.right = frame.left + w;
            frame.bottom = frame.top + h;
        }
    }

    // No caption: the app draws the band where the title bar would be, as the
    // band where the title bar would be. The frame stays, so resizing, snapping, Alt+Space
    // and the shadow all still work.
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"Search",
                            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU |
                                WS_CLIPCHILDREN,
                            frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top, nullptr,
                            nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        wchar_t msg[256];
        swprintf(msg, 256, L"CreateWindowExW failed: %lu", GetLastError());
        store::log(msg);
        return false;
    }

    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    columnWidth_ = store::settingInt(L"sidebar.width", metrics::side);
    columnWidth_ = std::max(metrics::sideMin, std::min(metrics::sideMax, columnWidth_));

    applyTheme();

    wv2::ensureEnvironment([this](bool ok) {
        if (!alive(this)) return;
        if (!ok) {
            store::log(L"environment not ready");
            return;
        }
        if (Tab* tab = active(); tab && !tab->controller) makeWebView(tab);
    });

    if (restore) {
        Session shape = session::read();
        for (auto& entry : shape.tabs) {
            Tab* tab = addTab(entry.url, true);
            if (!tab) continue;
            tab->title = entry.title;
            tab->name = entry.name;
            tab->pin = entry.pin;
            tab->home = entry.home;
            tab->muted = entry.muted;
            // A tab restored from last session costs nothing until it is
            // picked: no web view, no process.
            tab->asleep = true;
        }
        if (!tabs_.empty()) selectTab(std::max(0, std::min((int)shape.tabs.size() - 1, shape.active)));
    }
    if (tabs_.empty()) addInternalPage(L"newtab");

    layout();
    sessionTimer_ = SetTimer(hwnd_, kSessionTimer, 1200, nullptr);
    return true;
}

void BrowserWindow::showAndFocus() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    if (Tab* tab = active(); tab && tab->webview) {
        tab->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

void BrowserWindow::close() {
    writeSession();
    DestroyWindow(hwnd_);
}

void BrowserWindow::relayout() {
    layout();
    updateWebViewBounds();
    redraw();
}

void BrowserWindow::applyThemeNow() {
    applyTheme();
    relayout();
}

void BrowserWindow::reloadShield() {
    for (auto& tab : tabs_) {
        if (tab->webview) tab->webview->Reload();
    }
}

// ---- messages ----

LRESULT BrowserWindow::handle(UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            // Drawn into a picture and handed over in one go, so nothing
            // flickers as the column is repainted.
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, std::max(1, width_), std::max(1, height_));
            HGDIOBJ old = SelectObject(memory, bitmap);
            {
                Gdiplus::Graphics g(memory);
                paint(g);
            }
            BitBlt(dc, 0, 0, width_, height_, memory, 0, 0, SRCCOPY);
            SelectObject(memory, old);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            if (w != SIZE_MINIMIZED) onResize(LOWORD(l), HIWORD(l));
            return 0;

        case WM_DPICHANGED: {
            scale_ = HIWORD(w) / 96.0;
            g_theme.build(scale_, g_theme.dark);
            RECT* suggested = (RECT*)l;
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            relayout();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* info = (MINMAXINFO*)l;
            info->ptMinTrackSize.x = g_theme.px(640);
            info->ptMinTrackSize.y = g_theme.px(420);
            return 0;
        }

        case WM_NCCALCSIZE:
            if (w == TRUE) {
                // The whole window is client area: no caption drawn by the
                // system. A maximised window has its frame taken off by
                // Windows, so the work area is given back here or the window
                // hangs off the screen.
                if (IsZoomed(hwnd_)) {
                    auto* params = (NCCALCSIZE_PARAMS*)l;
                    MONITORINFO mi{sizeof(mi)};
                    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
                    params->rgrc[0] = mi.rcWork;
                }
                return 0;
            }
            break;

        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(hwnd_, &p);
            int border = g_theme.px(6);
            if (!IsZoomed(hwnd_)) {
                if (p.y < border && p.x < border) return HTTOPLEFT;
                if (p.y < border && p.x >= width_ - border) return HTTOPRIGHT;
                if (p.y >= height_ - border && p.x < border) return HTBOTTOMLEFT;
                if (p.y >= height_ - border && p.x >= width_ - border) return HTBOTTOMRIGHT;
                if (p.y < border) return HTTOP;
                if (p.y >= height_ - border) return HTBOTTOM;
                if (p.x < border) return HTLEFT;
                if (p.x >= width_ - border) return HTRIGHT;
            }
            // The band at the top, where there is nothing to press, drags the
            // window, as a title bar does.
            Gdiplus::RectF band(lay_.column.Width, 0, (Gdiplus::REAL)(width_ - lay_.column.Width),
                                (Gdiplus::REAL)g_theme.px(metrics::strip));
            if (band.Contains((Gdiplus::REAL)p.x, (Gdiplus::REAL)p.y)) {
                // The caption buttons are drawn there, and the field can be.
                for (auto& box : lay_.caption) {
                    if (box.Contains((Gdiplus::REAL)p.x, (Gdiplus::REAL)p.y)) return HTCLIENT;
                }
                if (fieldShown_ && lay_.field.Contains((Gdiplus::REAL)p.x, (Gdiplus::REAL)p.y)) return HTCLIENT;
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            applyTheme();
            redraw();
            return 0;

        case WM_ACTIVATE: {
            onActivate(LOWORD(w) != WA_INACTIVE);
            return 0;
        }


        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            onMouseMove(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        }
        case WM_MOUSELEAVE:
            onMouseLeave();
            return 0;

        case WM_LBUTTONDOWN:
            SetFocus(hwnd_);
            onMouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), 0, 1);
            return 0;
        case WM_LBUTTONDBLCLK:
            onMouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), 0, 2);
            return 0;
        case WM_MBUTTONDOWN:
            onMouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), 1, 1);
            return 0;
        case WM_RBUTTONDOWN:
            onMouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), 2, 1);
            return 0;
        case WM_LBUTTONUP:
            onMouseUp(GET_X_LPARAM(l), GET_Y_LPARAM(l), 0);
            return 0;
        case WM_MBUTTONUP:
            onMouseUp(GET_X_LPARAM(l), GET_Y_LPARAM(l), 1);
            return 0;

        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(hwnd_, &p);
            wheel(p.x, p.y, GET_WHEEL_DELTA_WPARAM(w), (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) != 0);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (key((int)w, control, shift, alt, (l & (1 << 30)) != 0)) return 0;
            break;
        }

        case WM_CHAR:
            if (fieldFocused_) {
                character((wchar_t)w);
                return 0;
            }
            break;

        case WM_TIMER:
            if (w == kSessionTimer) {
                // Kept running, so the session on disk follows the tabs as
                // they come and go: a window that is never closed cleanly
                // still comes back where it was.
                writeSession();
                return 0;
            }
            if (w == kCaretTimer) {
                caretOn_ = !caretOn_;
                if (fieldFocused_) redraw();
                return 0;
            }
            break;

        case WM_COMMAND:
            command(LOWORD(w));
            return 0;

        // A shortcut the page had, sent on from its own callback: the page has
        // let go of it, and the window does what it says.
        case WM_APP + 2: {
            bool shift = (l & 1) != 0;
            bool alt = (l & 2) != 0;
            if (key((int)w, true, shift, alt, false)) return 0;
            return 0;
        }

        case WM_SETFOCUS:
            if (Tab* tab = active(); tab && tab->webview && !fieldFocused_) {
                tab->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            }
            return 0;

        case WM_CLOSE:
            close();
            return 0;

        case WM_DESTROY:
            if (sessionTimer_) KillTimer(hwnd_, sessionTimer_);
            if (caretTimer_) KillTimer(hwnd_, caretTimer_);
            app::removeWindow(this);
            return 0;
    }
    return DefWindowProcW(hwnd_, message, w, l);
}
