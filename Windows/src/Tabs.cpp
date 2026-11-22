// The tabs and their pages: making a browser when a tab is first shown,
// handling what the page says back in the row, and the session that comes
// back at launch. Everything that speaks CEF lives in WebView.cpp.
#include "Window.h"

#include <shlwapi.h>

#include <algorithm>
#include <functional>
#include <set>

#include "Address.h"
#include "App.h"
#include "Data.h"
#include "Internal.h"
#include "Shield.h"
#include "Store.h"
#include "WebView.h"

// ---- tabs ----

Tab* BrowserWindow::addTab(const std::wstring& url, bool background) {
    auto tab = std::make_unique<Tab>();
    tab->window = this;
    tab->id = nextId_++;
    tab->url = url;
    tab->pageHost = address::host(url);
    Tab* raw = tab.get();
    tabs_.push_back(std::move(tab));
    if (!background) {
        selectTab((int)tabs_.size() - 1);
    } else {
        relayout();
    }
    return raw;
}

Tab* BrowserWindow::addInternalPage(const std::wstring& page, bool background) {
    auto tab = std::make_unique<Tab>();
    tab->window = this;
    tab->id = nextId_++;
    tab->internal = internalPath(page);
    tab->title = internalTitle(tab->internal);
    Tab* raw = tab.get();
    tabs_.push_back(std::move(tab));
    if (!background) {
        selectTab((int)tabs_.size() - 1);
    } else {
        relayout();
    }
    return raw;
}

void BrowserWindow::openInternalPage(const std::wstring& page) {
    // One tab of each kind per window: asking again goes to the one already
    // open.
    std::wstring path = internalPath(page);
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i]->internal == path) {
            selectTab((int)i);
            return;
        }
    }
    addInternalPage(path);
}

int BrowserWindow::indexOf(Tab* tab) const {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].get() == tab) return (int)i;
    }
    return -1;
}

Tab* BrowserWindow::active() const {
    if (active_ < 0 || active_ >= (int)tabs_.size()) return nullptr;
    return tabs_[active_].get();
}

void BrowserWindow::closeTab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    Tab* tab = tabs_[index].get();

    // A closed tab can come back with Ctrl+Shift+T.
    if (!tab->isInternal() && !tab->url.empty()) {
        auto closed = session::readClosed();
        closed.push_back({tab->url, tab->shownTitle(), index});
        if (closed.size() > 25) closed.erase(closed.begin());
        session::writeClosed(closed);
    }

    killWebView(tab);
    bool wasActive = index == active_;
    tabs_.erase(tabs_.begin() + index);

    if (tabs_.empty()) {
        active_ = -1;
        addInternalPage(L"newtab");
        return;
    }
    if (wasActive) {
        selectTab(std::min(index, (int)tabs_.size() - 1));
    } else if (index < active_) {
        --active_;
        relayout();
    } else {
        relayout();
    }
    updateTitle();
}

void BrowserWindow::selectTab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    if (index == active_ && engine::has(tabs_[index].get())) {
        attach(tabs_[index].get());
        return;
    }
    if (active_ >= 0 && active_ < (int)tabs_.size()) detach(tabs_[active_].get());
    active_ = index;
    Tab* tab = tabs_[index].get();

    // A tab restored from last session costs nothing until it is picked: this
    // is where it wakes, with the page it was left on.
    tab->asleep = false;
    if (!engine::has(tab)) {
        makeWebView(tab);
    } else {
        attach(tab);
    }
    updateTitle();
    relayout();
}

void BrowserWindow::moveTab(int from, int to) {
    if (from < 0 || from >= (int)tabs_.size() || to < 0 || to > (int)tabs_.size()) return;
    if (from == to) return;
    auto tab = std::move(tabs_[from]);
    tabs_.erase(tabs_.begin() + from);
    if (to > from) --to;
    tabs_.insert(tabs_.begin() + to, std::move(tab));
    active_ = to;
    relayout();
}

void BrowserWindow::reopenClosedTab() {
    auto closed = session::readClosed();
    if (closed.empty()) return;
    auto last = closed.back();
    closed.pop_back();
    session::writeClosed(closed);
    Tab* tab = addTab(last.url);
    tab->title = last.title;
}

// ---- the web view ----

void BrowserWindow::makeWebView(Tab* tab) {
    if (!tab) return;
    std::wstring url = tab->isInternal() ? internalURL(tab->internal) : tab->url;
    if (!engine::create(tab, hwnd_, url)) {
        wchar_t msg[256];
        swprintf(msg, 256, L"CEF could not create a browser for %ls", url.c_str());
        store::log(msg);
        tab->crashed = true;
        relayout();
    }
}

void BrowserWindow::attach(Tab* tab) {
    if (!tab || !engine::has(tab)) return;
    engine::setVisible(tab, true);
    updateWebViewBounds();
}

void BrowserWindow::detach(Tab* tab) {
    if (tab && engine::has(tab)) engine::setVisible(tab, false);
}

void BrowserWindow::killWebView(Tab* tab) {
    if (tab && engine::has(tab)) engine::destroy(tab);
}

void BrowserWindow::updateWebViewBounds() {
    Tab* tab = active();
    if (!tab || !engine::has(tab)) return;

    RECT page{};
    page.left = (LONG)lay_.page.X;
    page.top = (LONG)lay_.page.Y;
    page.right = (LONG)(lay_.page.X + lay_.page.Width);
    page.bottom = (LONG)(lay_.page.Y + lay_.page.Height);

    std::vector<RECT> caption;
    for (const auto& r : lay_.caption) {
        caption.push_back({(LONG)r.X, (LONG)r.Y, (LONG)r.GetRight(), (LONG)r.GetBottom()});
    }
    engine::setBounds(tab, page, caption, g_theme.px(metrics::cardRadius));
}
// ---- what a page says ----

void BrowserWindow::pageTitleChanged(Tab* tab, const std::wstring& title) {
    tab->title = title;
    if (tab == active()) updateTitle();
    redraw();
}

void BrowserWindow::pageURLChanged(Tab* tab, const std::wstring& url) {
    if (url.rfind(L"https://search.internal/", 0) == 0) return;
    tab->url = url;
    tab->pageHost = address::host(url);
    if (tab == active() && fieldShown_ && !fieldFocused_) {
        fieldText_ = address::editable(url);
        fieldCaret_ = (int)fieldText_.size();
    }
    redraw();
}

void BrowserWindow::pageLoading(Tab* tab, bool loading) {
    tab->loading = loading;
    redraw();
}

void BrowserWindow::pageHistory(Tab* tab, bool canBack, bool canForward) {
    tab->canGoBack = canBack;
    tab->canGoForward = canForward;
    redraw();
}

void BrowserWindow::pageCrashed(Tab* tab) {
    tab->crashed = true;
    redraw();
}

// ---- navigation ----

void BrowserWindow::navigate(const std::wstring& url) {
    Tab* tab = active();
    if (!tab) return;
    showField(false);

    if (app::isInternal(url)) {
        tab->internal = internalPath(url);
        tab->url.clear();
        tab->title = internalTitle(tab->internal);
        if (engine::has(tab)) {
            engine::loadHTML(tab, internalHTML(tab->internal), internalURL(tab->internal));
        } else {
            makeWebView(tab);
        }
        updateTitle();
        relayout();
        return;
    }

    tab->internal.clear();
    tab->url = url;
    tab->title.clear();
    tab->pageHost = address::host(url);
    if (!engine::has(tab)) {
        makeWebView(tab);
    } else {
        engine::navigate(tab, url);
    }
    updateTitle();
    relayout();
}

void BrowserWindow::goBack() {
    if (Tab* tab = active(); tab && engine::has(tab)) engine::goBack(tab);
}

void BrowserWindow::goForward() {
    if (Tab* tab = active(); tab && engine::has(tab)) engine::goForward(tab);
}

void BrowserWindow::reload() {
    Tab* tab = active();
    if (!tab) return;
    if (tab->isInternal()) {
        if (engine::has(tab)) engine::loadHTML(tab, internalHTML(tab->internal), internalURL(tab->internal));
        return;
    }
    if (!engine::has(tab)) {
        makeWebView(tab);
        return;
    }
    engine::reload(tab);
}

void BrowserWindow::stop() {
    if (Tab* tab = active(); tab && engine::has(tab)) engine::stop(tab);
}

// ---- the field ----

void BrowserWindow::showField(bool shown) {
    if (fieldShown_ == shown) {
        if (shown) relayout();
        return;
    }
    fieldShown_ = shown;
    if (shown) {
        Tab* tab = active();
        fieldText_ = tab && !tab->isInternal() ? address::editable(tab->shownURL()) : L"";
        fieldCaret_ = (int)fieldText_.size();
        fieldSelectAll_ = true;
        fieldFocused_ = true;
        caretOn_ = true;
        // The keys go to the window while the field is up, so the window has
        // to have them.
        SetFocus(hwnd_);
        if (caretTimer_) KillTimer(hwnd_, caretTimer_);
        caretTimer_ = SetTimer(hwnd_, kCaretTimer, 530, nullptr);
        updateSuggestions();
    } else {
        fieldFocused_ = false;
        suggestions_.clear();
        suggestionPicked_ = -1;
        if (caretTimer_) {
            KillTimer(hwnd_, caretTimer_);
            caretTimer_ = 0;
        }
    }
    relayout();
}

void BrowserWindow::focusField(bool selectAll) {
    showField(true);
    fieldSelectAll_ = selectAll;
}

void BrowserWindow::setFieldText(const std::wstring& text) {
    fieldText_ = text;
    fieldCaret_ = (int)text.size();
    fieldSelectAll_ = false;
    updateSuggestions();
    redraw();
}

void BrowserWindow::updateSuggestions() {
    suggestions_.clear();
    suggestionPicked_ = -1;
    std::wstring text = address::trim(fieldText_);
    if (text.empty()) return;

    // What you could type to get here: the pages you have been to, the ones
    // you kept, and the ones open right now. Nothing is sent anywhere.
    std::wstring low = address::lower(text);
    std::set<std::wstring> seen;

    // What Return would do, first: an address, or words to ask the engine.
    Suggestion first;
    if (auto place = address::url(text)) {
        first.kind = Suggestion::Kind::Place;
        first.title = address::withoutWWW(address::pretty(*place));
        first.detail = *place;
        first.url = *place;
    } else {
        first.kind = Suggestion::Kind::Search;
        first.title = L"Search for “" + text + L"”";
        first.detail = currentEngine().title;
        first.url = app::searchURL(text);
    }
    suggestions_.push_back(first);
    seen.insert(low);

    auto matches = [&](const std::wstring& haystack) {
        return address::lower(haystack).find(low) != std::wstring::npos;
    };

    for (auto& tab : tabs_) {
        if (tab->isInternal() || tab->url.empty()) continue;
        if (!matches(tab->url) && !matches(tab->shownTitle())) continue;
        if (!seen.insert(address::lower(tab->url)).second) continue;
        suggestions_.push_back({tab->shownTitle(), L"Open tab", tab->url, Suggestion::Kind::Place});
        if (suggestions_.size() >= 7) break;
    }
    if (suggestions_.size() < 7) {
        for (auto& visit : history::read()) {
            if (!matches(visit.url) && !matches(visit.title)) continue;
            if (!seen.insert(address::lower(visit.url)).second) continue;
            suggestions_.push_back({visit.title.empty() ? address::pretty(visit.url) : visit.title,
                                    address::pretty(visit.url), visit.url, Suggestion::Kind::History});
            if (suggestions_.size() >= 7) break;
        }
    }
    if (suggestions_.size() < 7) {
        std::function<void(const std::vector<Bookmark>&)> walk = [&](const std::vector<Bookmark>& list) {
            for (auto& b : list) {
                if (suggestions_.size() >= 7) return;
                if (b.isFolder()) {
                    walk(b.children);
                    continue;
                }
                if (!matches(b.url) && !matches(b.title)) continue;
                if (!seen.insert(address::lower(b.url)).second) continue;
                suggestions_.push_back({b.title.empty() ? address::pretty(b.url) : b.title,
                                        address::pretty(b.url), b.url, Suggestion::Kind::Bookmark});
            }
        };
        walk(bookmarks::read());
    }

    // The list has changed, so the room the page leaves changes with it:
    // taller for more rows, shorter for fewer.
    if (fieldShown_) relayout();
}

void BrowserWindow::takeSuggestion(int index) {
    if (index < 0 || index >= (int)suggestions_.size()) return;
    navigate(suggestions_[index].url);
}

void BrowserWindow::submitField() {
    if (suggestionPicked_ >= 0 && suggestionPicked_ < (int)suggestions_.size()) {
        takeSuggestion(suggestionPicked_);
        return;
    }
    std::wstring text = address::trim(fieldText_);
    if (text.empty()) {
        showField(false);
        return;
    }
    std::wstring url = app::resolve(text);
    if (url.empty()) {
        showField(false);
        return;
    }
    navigate(url);
}

// ---- a tab's own commands ----

void BrowserWindow::pinActive(bool pinned) {
    Tab* tab = active();
    if (!tab || tab->isInternal()) return;
    if (pinned) {
        tab->pin = tab->shownTitle().empty() ? L"•" : tab->shownTitle().substr(0, 1);
        tab->home = tab->url;
    } else {
        tab->pin.clear();
    }
    relayout();
}

void BrowserWindow::duplicateActive() {
    Tab* tab = active();
    if (!tab) return;
    if (tab->isInternal()) {
        addInternalPage(tab->internal);
        return;
    }
    Tab* copy = addTab(tab->url);
    copy->title = tab->title;
}

void BrowserWindow::muteActive() {
    Tab* tab = active();
    if (!tab) return;
    tab->muted = !tab->muted;
    if (engine::has(tab)) engine::setMuted(tab, tab->muted);
    redraw();
}

bool BrowserWindow::shieldPausedHere() const {
    Tab* tab = active();
    if (!tab) return false;
    return shield::pausedOn(tab->pageHost);
}

// ---- the session ----

void BrowserWindow::writeSession() {
    Session shape;
    for (auto& tab : tabs_) {
        if (tab->isInternal() || tab->url.empty()) continue;
        SessionTab entry;
        entry.url = tab->url;
        entry.title = tab->shownTitle();
        entry.pin = tab->pin;
        entry.name = tab->name;
        entry.home = tab->home;
        entry.muted = tab->muted;
        if (tab.get() == active()) shape.active = (int)shape.tabs.size();
        shape.tabs.push_back(entry);
    }
    // Written even when there is nothing to restore: a session left over from
    // last time would otherwise bring back tabs that were closed.
    session::write(shape);

    RECT frame{};
    if (GetWindowRect(hwnd_, &frame)) {
        wchar_t buf[128];
        swprintf(buf, 128, L"%ld,%ld,%ld,%ld", frame.left, frame.top, frame.right, frame.bottom);
        store::setSetting(L"window.frame", buf);
    }
}

// ---- the field's room ----

// The field and its suggestions are the window's own painting; the page is
// CEF's child window, which Windows composites above everything the window
// draws, and which neither a region nor a window of the app's own can be made
// to cover. So while the field is up, the page steps down below it (see
// layout()), and comes back up when the field goes.
void BrowserWindow::paintSheet() { relayout(); }
