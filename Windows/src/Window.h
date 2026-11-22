// A window: a column of tabs down the left, the page taking the rest, and one
// field. Typing an address gets you a page; there is nothing else to learn and
// nothing else to press.
//
// Windows' own caption is not drawn. The column's band takes the title bar's
// place, with back, forward and reload where the Mac has them, and the
// system's minimise, maximise and close are drawn at the top right where
// Windows puts them, faint until the pointer comes near.
#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <wrl.h>
#include <WebView2.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Tab.h"

class BrowserWindow {
public:
    BrowserWindow();
    ~BrowserWindow();

    static void registerClass();
    bool create(bool restoreSession);
    HWND hwnd() const { return hwnd_; }
    // During WM_NCCREATE, before CreateWindowExW has returned, the window's
    // handle is only known to the message: this is how it is kept.
    void setHwnd(HWND hwnd) { hwnd_ = hwnd; }

    // ---- tabs ----
    Tab* addTab(const std::wstring& url, bool background = false);
    Tab* addInternalPage(const std::wstring& page, bool background = false);
    void openInternalPage(const std::wstring& page);
    void closeTab(int index);
    void selectTab(int index);
    void moveTab(int from, int to);
    Tab* active() const;
    int activeIndex() const { return active_; }
    int indexOf(Tab* tab) const;
    void reopenClosedTab();

    // ---- navigation ----
    void navigate(const std::wstring& url);
    void goBack();
    void goForward();
    void reload();
    void stop();

    // ---- the field ----
    void focusField(bool selectAll);
    void showField(bool shown);
    bool fieldShown() const { return fieldShown_; }
    void setFieldText(const std::wstring& text);

    // ---- a tab's own commands ----
    void pinActive(bool pinned);
    void duplicateActive();
    void muteActive();
    bool shieldPausedHere() const;

    // ---- the window ----
    void showAndFocus();
    void close();
    void writeSession();
    void applyThemeNow();
    void relayout();
    void reloadShield();

    LRESULT handle(UINT message, WPARAM w, LPARAM l);

    // Called by a tab's page, and by the environment's callback.
    void makeWebView(Tab* tab);
    void wire(Tab* tab);
    void attach(Tab* tab);
    void pageTitleChanged(Tab* tab, const std::wstring& title);
    void pageURLChanged(Tab* tab, const std::wstring& url);
    void pageLoading(Tab* tab, bool loading);
    void pageHistory(Tab* tab, bool canBack, bool canForward);
    void pageCrashed(Tab* tab);

private:
    void detach(Tab* tab);
    void killWebView(Tab* tab);
    void updateWebViewBounds();
    void updateTitle();
    void applyTheme();
    void redraw();

    void layout();
    void paint(Gdiplus::Graphics& g);
    void paintBand(Gdiplus::Graphics& g);
    void paintColumn(Gdiplus::Graphics& g);
    void paintRow(Gdiplus::Graphics& g, Tab* tab, const Gdiplus::RectF& row, bool live, bool over);
    void paintPage(Gdiplus::Graphics& g);
    void paintField(Gdiplus::Graphics& g);
    // The field, and the suggestions hanging under it, as one rectangle.
    Gdiplus::RectF fieldArea() const;
    void paintSheet();
    void paintSuggestions(Gdiplus::Graphics& g);

    void onResize(int width, int height);
    void onActivate(bool active);
    void onMouseMove(int x, int y);
    void onMouseDown(int x, int y, int button, int clicks);
    void onMouseUp(int x, int y, int button);
    void onMouseLeave();
    void wheel(int x, int y, int delta, bool control);
    bool key(int vk, bool control, bool shift, bool alt, bool repeat);
    void character(wchar_t c);
    void command(UINT id);
    void showTabMenu(int index, int x, int y);

    void updateSuggestions();
    void takeSuggestion(int index);
    void submitField();
    void runInternalAction(const std::wstring& url);

    std::vector<Tab*> pinTabs() const;
    std::vector<Tab*> rowTabs() const;

    struct Layout {
        Gdiplus::RectF column;
        Gdiplus::RectF page;
        Gdiplus::RectF field;
        Gdiplus::RectF newTab;
        std::vector<Gdiplus::RectF> pins;
        std::vector<Gdiplus::RectF> rows;
        std::vector<Gdiplus::RectF> doors;    // back, forward, reload
        std::vector<Gdiplus::RectF> caption;  // minimise, maximise, close
    };
    Layout lay_;

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;
    double scale_ = 1.0;

    std::vector<std::unique_ptr<Tab>> tabs_;
    int active_ = -1;
    // Whether this window is the one in front, which the field's sheet follows.
    int nextId_ = 1;
    int columnWidth_ = 232;

    bool fieldShown_ = false;
    bool fieldFocused_ = false;
    bool fieldSelectAll_ = false;
    std::wstring fieldText_;
    int fieldCaret_ = 0;
    bool caretOn_ = true;

    enum class Hot { None, Row, Pin, Plus, Back, Forward, Reload, Field, Minimise, Maximise, Close };
    Hot hot_ = Hot::None;
    int hotIndex_ = -1;

    bool draggingEdge_ = false;
    int dragStartX_ = 0;
    int dragStartWidth_ = 0;
    int pressRow_ = -1;
    bool rowPressed_ = false;
    int dragRow_ = -1;
    int dragTo_ = -1;
    int pressX_ = 0, pressY_ = 0;

    struct Suggestion {
        std::wstring title;
        std::wstring detail;
        std::wstring url;
        enum class Kind { Place, Search, History, Bookmark, Internal } kind = Kind::Place;
    };
    std::vector<Suggestion> suggestions_;
    int suggestionPicked_ = -1;

    UINT_PTR sessionTimer_ = 0;
    UINT_PTR caretTimer_ = 0;

};

// The windows that still exist, so a callback that arrives after its window
// has gone does nothing rather than reach into freed memory.
std::set<BrowserWindow*>& liveWindows();

// The timers' ids, which a window and its tabs share.
constexpr UINT_PTR kSessionTimer = 1;
constexpr UINT_PTR kCaretTimer = 2;
