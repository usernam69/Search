// What the pointer and the keys do: the column's rows and pins, the three
// doors, the caption buttons, the field and its suggestions, and the
// shortcuts the New Tab page lists.
#include "Window.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>

#include "Address.h"
#include "App.h"
#include "Data.h"
#include "Internal.h"
#include "Shield.h"
#include "Store.h"
#include "WebView.h"

// ---- the pointer ----

static bool inside(const Gdiplus::RectF& r, int x, int y) {
    return r.Contains((Gdiplus::REAL)x, (Gdiplus::REAL)y);
}

void BrowserWindow::onMouseMove(int x, int y) {
    Hot was = hot_;
    int wasIndex = hotIndex_;
    hot_ = Hot::None;
    hotIndex_ = -1;

    // The field's suggestions come first: they are over everything.
    if (fieldShown_ && !suggestions_.empty()) {
        double rowHeight = g_theme.px(46);
        Gdiplus::RectF box(lay_.field.X, lay_.field.GetBottom() + (Gdiplus::REAL)g_theme.px(6), lay_.field.Width,
                           (Gdiplus::REAL)(rowHeight * suggestions_.size() + g_theme.px(12)));
        if (inside(box, x, y)) {
            int picked = (int)((y - box.Y - g_theme.px(6)) / rowHeight);
            if (picked >= 0 && picked < (int)suggestions_.size()) {
                suggestionPicked_ = picked;
                redraw();
            }
            return;
        }
    }

    if (fieldShown_ && inside(lay_.field, x, y)) {
        hot_ = Hot::Field;
        redraw();
        return;
    }

    for (size_t i = 0; i < lay_.caption.size(); ++i) {
        if (inside(lay_.caption[i], x, y)) {
            hot_ = (Hot)((int)Hot::Minimise + (int)i);
            redraw();
            return;
        }
    }
    for (size_t i = 0; i < lay_.doors.size(); ++i) {
        if (inside(lay_.doors[i], x, y)) {
            hot_ = (Hot)((int)Hot::Back + (int)i);
            redraw();
            return;
        }
    }
    for (size_t i = 0; i < lay_.pins.size(); ++i) {
        if (inside(lay_.pins[i], x, y)) {
            hot_ = Hot::Pin;
            hotIndex_ = (int)i;
            redraw();
            return;
        }
    }
    for (size_t i = 0; i < lay_.rows.size(); ++i) {
        if (inside(lay_.rows[i], x, y)) {
            hot_ = Hot::Row;
            hotIndex_ = (int)i;
            redraw();
            return;
        }
    }
    if (inside(lay_.newTab, x, y)) {
        hot_ = Hot::Plus;
        redraw();
        return;
    }
    if (hot_ != was || hotIndex_ != wasIndex) redraw();
}

void BrowserWindow::onMouseLeave() {
    if (hot_ == Hot::None && hotIndex_ == -1) return;
    hot_ = Hot::None;
    hotIndex_ = -1;
    redraw();
}

void BrowserWindow::onMouseDown(int x, int y, int button, int clicks) {
    // The field's suggestions: a click takes one.
    if (fieldShown_ && !suggestions_.empty()) {
        double rowHeight = g_theme.px(46);
        Gdiplus::RectF box(lay_.field.X, lay_.field.GetBottom() + (Gdiplus::REAL)g_theme.px(6), lay_.field.Width,
                           (Gdiplus::REAL)(rowHeight * suggestions_.size() + g_theme.px(12)));
        if (inside(box, x, y)) {
            int picked = (int)((y - box.Y - g_theme.px(6)) / rowHeight);
            if (picked >= 0 && picked < (int)suggestions_.size()) takeSuggestion(picked);
            return;
        }
    }
    if (fieldShown_ && inside(lay_.field, x, y)) {
        fieldFocused_ = true;
        redraw();
        return;
    }

    // The caption buttons.
    for (size_t i = 0; i < lay_.caption.size(); ++i) {
        if (!inside(lay_.caption[i], x, y)) continue;
        if (i == 0) ShowWindow(hwnd_, SW_MINIMIZE);
        if (i == 1) ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
        if (i == 2) close();
        return;
    }

    // The three doors.
    for (size_t i = 0; i < lay_.doors.size(); ++i) {
        if (!inside(lay_.doors[i], x, y)) continue;
        if (i == 0) goBack();
        if (i == 1) goForward();
        if (i == 2) (active() && active()->loading) ? stop() : reload();
        return;
    }

    // A pin, or a row.
    for (size_t i = 0; i < lay_.pins.size(); ++i) {
        if (!inside(lay_.pins[i], x, y)) continue;
        auto pins = pinTabs();
        if (i < pins.size()) selectTab(indexOf(pins[i]));
        return;
    }
    for (size_t i = 0; i < lay_.rows.size(); ++i) {
        if (!inside(lay_.rows[i], x, y)) continue;
        auto rows = rowTabs();
        if (i >= rows.size()) return;
        if (button == 1) {
            closeTab(indexOf(rows[i]));
            return;
        }
        if (button == 2) {
            showTabMenu(indexOf(rows[i]), x, y);
            return;
        }
        if (clicks == 2) {
            // A double-click on a tab names it, as a row does with its
            // own menu; here it is the simplest way to rename one.
            Tab* tab = rows[i];
            wchar_t buf[512];
            wcscpy_s(buf, tab->shownTitle().c_str());
            // A rename needs a real dialog, which is a later step.
            (void)buf;
            return;
        }
        selectTab(indexOf(rows[i]));
        return;
    }

    if (inside(lay_.newTab, x, y)) {
        addInternalPage(L"newtab");
        return;
    }

    // Anywhere else puts the field away, as Escape does.
    if (fieldShown_) showField(false);
}

void BrowserWindow::onMouseUp(int x, int y, int button) { (void)x, (void)y, (void)button; }

void BrowserWindow::wheel(int x, int y, int delta, bool control) {
    (void)control;
    // Over the column, the wheel walks the rows; over the page, the page has
    // it, as CEF handles its own scrolling.
    Gdiplus::RectF column(0, 0, lay_.column.Width, (Gdiplus::REAL)height_);
    if (!inside(column, x, y)) return;
    if (delta > 0) {
        if (active_ > 0) selectTab(active_ - 1);
    } else {
        if (active_ + 1 < (int)tabs_.size()) selectTab(active_ + 1);
    }
}

// ---- the keys ----

bool BrowserWindow::key(int vk, bool control, bool shift, bool alt, bool repeat) {
    (void)repeat;

    // The field, while it is up, takes the keys a field takes.
    if (fieldFocused_) {
        switch (vk) {
            case VK_ESCAPE:
                if (!suggestions_.empty()) {
                    suggestions_.clear();
                    suggestionPicked_ = -1;
                    redraw();
                } else {
                    showField(false);
                }
                return true;
            case VK_RETURN:
                submitField();
                return true;
            case VK_BACK:
                if (fieldSelectAll_) {
                    fieldText_.clear();
                    fieldCaret_ = 0;
                    fieldSelectAll_ = false;
                } else if (fieldCaret_ > 0) {
                    fieldText_.erase(fieldCaret_ - 1, 1);
                    --fieldCaret_;
                }
                updateSuggestions();
                redraw();
                return true;
            case VK_DELETE:
                if (fieldSelectAll_) {
                    fieldText_.clear();
                    fieldCaret_ = 0;
                    fieldSelectAll_ = false;
                } else if (fieldCaret_ < (int)fieldText_.size()) {
                    fieldText_.erase(fieldCaret_, 1);
                }
                updateSuggestions();
                redraw();
                return true;
            case VK_LEFT:
                if (fieldSelectAll_) {
                    fieldSelectAll_ = false;
                } else if (fieldCaret_ > 0) {
                    --fieldCaret_;
                }
                redraw();
                return true;
            case VK_RIGHT:
                if (fieldSelectAll_) {
                    fieldSelectAll_ = false;
                    fieldCaret_ = (int)fieldText_.size();
                } else if (fieldCaret_ < (int)fieldText_.size()) {
                    ++fieldCaret_;
                }
                redraw();
                return true;
            case VK_HOME:
                fieldCaret_ = 0;
                fieldSelectAll_ = false;
                redraw();
                return true;
            case VK_END:
                fieldCaret_ = (int)fieldText_.size();
                fieldSelectAll_ = false;
                redraw();
                return true;
            case VK_DOWN:
                if (!suggestions_.empty()) {
                    suggestionPicked_ = (std::min)((int)suggestions_.size() - 1, suggestionPicked_ + 1);
                    redraw();
                }
                return true;
            case VK_UP:
                if (!suggestions_.empty()) {
                    suggestionPicked_ = (std::max)(0, suggestionPicked_ - 1);
                    redraw();
                }
                return true;
            case 'A':
                if (control) {
                    fieldSelectAll_ = true;
                    redraw();
                    return true;
                }
                break;
            case 'C':
                if (control) {
                    if (OpenClipboard(hwnd_)) {
                        EmptyClipboard();
                        size_t bytes = (fieldText_.size() + 1) * sizeof(wchar_t);
                        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                        if (mem) {
                            memcpy(GlobalLock(mem), fieldText_.c_str(), bytes);
                            GlobalUnlock(mem);
                            SetClipboardData(CF_UNICODETEXT, mem);
                        }
                        CloseClipboard();
                    }
                    return true;
                }
                break;
            case 'V':
                if (control) {
                    if (OpenClipboard(hwnd_)) {
                        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
                            auto* text = (const wchar_t*)GlobalLock(data);
                            if (text) {
                                std::wstring pasted = text;
                                GlobalUnlock(data);
                                std::wstring flat;
                                for (wchar_t c : pasted) {
                                    if (c != L'\r' && c != L'\n') flat += c;
                                }
                                if (fieldSelectAll_) {
                                    fieldText_ = flat;
                                    fieldCaret_ = (int)flat.size();
                                } else {
                                    fieldText_.insert(fieldCaret_, flat);
                                    fieldCaret_ += (int)flat.size();
                                }
                            }
                        }
                        CloseClipboard();
                    }
                    fieldSelectAll_ = false;
                    updateSuggestions();
                    redraw();
                    return true;
                }
                break;
            default:
                break;
        }
        if (control && vk == 'L') return true;
    }

    // The shortcuts, all on Ctrl.
    if (control && !alt) {
        switch (vk) {
            case 'T':
                if (shift) {
                    reopenClosedTab();
                } else {
                    addInternalPage(L"newtab");
                }
                return true;
            case 'W':
                if (shift) {
                    close();
                } else {
                    closeTab(active_);
                }
                return true;
            case 'L':
                focusField(true);
                return true;
            case 'R':
                reload();
                return true;
            case 'D':
                if (Tab* tab = active()) {
                    auto list = bookmarks::read();
                    list.push_back({L"", tab->shownTitle(), tab->url, {}});
                    bookmarks::write(list);
                }
                return true;
            case 'H':
                openInternalPage(L"history");
                return true;
            case 'J':
                openInternalPage(L"downloads");
                return true;
            case ',': {
                openInternalPage(L"settings");
                return true;
            }
            case VK_TAB:
                if (shift) {
                    selectTab(active_ - 1 < 0 ? (int)tabs_.size() - 1 : active_ - 1);
                } else {
                    selectTab((active_ + 1) % (int)tabs_.size());
                }
                return true;
            case VK_PRIOR:
                selectTab(active_ - 1 < 0 ? (int)tabs_.size() - 1 : active_ - 1);
                return true;
            case VK_NEXT:
                selectTab((active_ + 1) % (int)tabs_.size());
                return true;
            case 'N':
                if (shift) {
                    // A window of its own.
                    extern void newWindow();
                    newWindow();
                }
                return true;
            case 'Q':
                app::quit(true);
                return true;
            case 'F':
            case 'K':
                // Find and the tab switcher are the page's own in CEF.
                return false;
            default:
                if (vk >= '1' && vk <= '9') {
                    int index = vk - '1';
                    if (index < (int)tabs_.size()) selectTab(index);
                    return true;
                }
                break;
        }
    }

    // Alt+Left and Alt+Right go back and forward, as most browsers do.
    if (alt && vk == VK_LEFT) {
        goBack();
        return true;
    }
    if (alt && vk == VK_RIGHT) {
        goForward();
        return true;
    }
    if (vk == VK_F5) {
        reload();
        return true;
    }
    if (vk == VK_F6 || (control && vk == 'E')) {
        focusField(true);
        return true;
    }
    if (vk == VK_ESCAPE) {
        if (fieldShown_) {
            showField(false);
            return true;
        }
        return false;
    }
    return false;
}

void BrowserWindow::character(wchar_t c) {
    if (!fieldFocused_) return;
    if (c < 0x20 || c == 0x7F) return;
    if (fieldSelectAll_) {
        fieldText_.clear();
        fieldCaret_ = 0;
        fieldSelectAll_ = false;
    }
    fieldText_.insert(fieldText_.begin() + fieldCaret_, c);
    ++fieldCaret_;
    updateSuggestions();
    redraw();
}

void BrowserWindow::command(UINT id) {
    if (id == 1) addInternalPage(L"newtab");
}

void BrowserWindow::showTabMenu(int index, int x, int y) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    Tab* tab = tabs_[index].get();

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 100, L"Close Tab");
    AppendMenuW(menu, MF_STRING, 101, L"Duplicate");
    AppendMenuW(menu, MF_STRING, 102, tab->isPinned() ? L"Unpin Tab" : L"Pin Tab");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 103, L"Copy Address");

    POINT at{x, y};
    ClientToScreen(hwnd_, &at);
    int picked = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (picked) {
        case 100: closeTab(index); break;
        case 101:
            if (tab->isInternal()) {
                addInternalPage(tab->internal);
            } else {
                Tab* copy = addTab(tab->url);
                copy->title = tab->title;
            }
            break;
        case 102: {
            int was = active_;
            selectTab(index);
            pinActive(!tab->isPinned());
            (void)was;
            break;
        }
        case 103:
            if (OpenClipboard(hwnd_)) {
                EmptyClipboard();
                size_t bytes = (tab->shownURL().size() + 1) * sizeof(wchar_t);
                HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (mem) {
                    memcpy(GlobalLock(mem), tab->shownURL().c_str(), bytes);
                    GlobalUnlock(mem);
                    SetClipboardData(CF_UNICODETEXT, mem);
                }
                CloseClipboard();
            }
            break;
        default:
            break;
    }
}
