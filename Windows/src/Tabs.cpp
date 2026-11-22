// The tabs and their pages: making a web view when a tab is first shown,
// wiring what the page says back into the row, and the session that comes
// back at launch.
#include "Window.h"

#include <shlwapi.h>
#include <wrl/event.h>

#include <algorithm>
#include <functional>
#include <set>

#include "Address.h"
#include "Com.h"
#include "App.h"
#include "Data.h"
#include "Internal.h"
#include "Shield.h"
#include "Store.h"
#include "WebView.h"

using namespace Microsoft::WRL;

static bool alive(BrowserWindow* window) { return liveWindows().count(window) > 0; }

class ControllerHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                             ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
public:
    BrowserWindow* window = nullptr;
    Tab* tab = nullptr;

    STDMETHODIMP Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (!tab || !window || !alive(window)) return S_OK;
        if (FAILED(result) || !controller) {
            { wchar_t m[160]; swprintf(m,160,L"controller failed: 0x%08lx", (unsigned long)result); store::log(m); }
            tab->crashed = true;
            window->relayout();
            return S_OK;
        }
        tab->controller = controller;
        controller->get_CoreWebView2(&tab->webview);
        window->wire(tab);
        return S_OK;
    }
};

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
    // open, as on the Mac.
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

    // A closed tab can come back with Ctrl+Shift+T, as on the Mac.
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
    if (index == active_ && tabs_[index]->controller) {
        attach(tabs_[index].get());
        return;
    }
    if (active_ >= 0 && active_ < (int)tabs_.size()) detach(tabs_[active_].get());
    active_ = index;
    Tab* tab = tabs_[index].get();

    // A tab restored from last session costs nothing until it is picked: this
    // is where it wakes, with the page it was left on.
    tab->asleep = false;
    if (!tab->controller) {
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
    if (!wv2::environment()) {
        // The environment is still being made; it asks for the tab again when
        // it is ready.
        wv2::ensureEnvironment([this](bool ok) {
            if (!alive(this) || !ok) return;
            if (Tab* front = active(); front && !front->controller) makeWebView(front);
        });
        return;
    }

    ComPtr<ControllerHandler> done = Make<ControllerHandler>();
    done->window = this;
    done->tab = tab;

    ComPtr<ICoreWebView2Environment10> env10;
    wv2::environment().As(&env10);
    if (env10) {
        ComPtr<ICoreWebView2ControllerOptions> options;
        if (SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&options)) && options) {
            options->put_IsInPrivateModeEnabled(FALSE);
            env10->CreateCoreWebView2ControllerWithOptions(hwnd_, options.Get(), done.Get());
            return;
        }
    }
    wv2::environment()->CreateCoreWebView2Controller(hwnd_, done.Get());
}

void BrowserWindow::attach(Tab* tab) {
    if (!tab || !tab->controller) return;
    tab->controller->put_IsVisible(TRUE);
    updateWebViewBounds();
    
}

void BrowserWindow::detach(Tab* tab) {
    if (tab && tab->controller) tab->controller->put_IsVisible(FALSE);
}

void BrowserWindow::killWebView(Tab* tab) {
    if (!tab || !tab->controller) return;
    tab->controller->Close();
    tab->controller.Reset();
    tab->webview.Reset();
}

void BrowserWindow::updateWebViewBounds() {
    Tab* tab = active();
    if (!tab || !tab->controller) return;
    RECT page;
    page.left = (LONG)lay_.page.X;
    page.top = (LONG)lay_.page.Y;
    page.right = (LONG)(lay_.page.X + lay_.page.Width);
    page.bottom = (LONG)(lay_.page.Y + lay_.page.Height);
    tab->controller->put_Bounds(page);

    // The page is a card with its corners round, and the caption buttons sit
    // over its top right. WebView2's own window is a plain rectangle, so the
    // corners and the caption are cut out of it. It is a child of the browser
    // window, and the only child it has, so it is found rather than assumed:
    // the controller hands back the window it was given, not the one it made.
    HWND host = nullptr;
    EnumChildWindows(
        hwnd_,
        [](HWND child, LPARAM found) -> BOOL {
            // The web view makes two windows of that class: an empty one that
            // holds nothing, and the one the page is drawn in. It is the one
            // with a size that is cut.
            wchar_t name[64] = {};
            GetClassNameW(child, name, 64);
            if (wcsncmp(name, L"Chrome_WidgetWin", 16) != 0) return TRUE;
            RECT box{};
            GetWindowRect(child, &box);
            if (box.right - box.left < 2 || box.bottom - box.top < 2) return TRUE;
            *(HWND*)found = child;
            return FALSE;
        },
        (LPARAM)&host);
    if (!host) return;

    int radius = g_theme.px(metrics::cardRadius);
    HRGN region = CreateRoundRectRgn(0, 0, page.right - page.left + 1, page.bottom - page.top + 1, radius * 2,
                                     radius * 2);
    if (!lay_.caption.empty()) {
        const Gdiplus::RectF& last = lay_.caption.back();
        HRGN corner = CreateRectRgn((int)last.X - page.left, 0, (int)(last.GetRight()) - page.left,
                                    (int)last.GetBottom());
        CombineRgn(region, region, corner, RGN_DIFF);
        DeleteObject(corner);
    }

    // The region goes on the host window, which is the one WebView2 sizes.
    SetWindowRgn(host, region, TRUE);
    // The region belongs to the window now.
}

void BrowserWindow::wire(Tab* tab) {
    if (!tab || !tab->webview) return;
    ICoreWebView2* web = tab->webview.Get();

    // Nothing of the browser's own furniture: pages look like pages.
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(web->get_Settings(&settings)) && settings) {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(TRUE);
        settings->put_AreDefaultContextMenusEnabled(TRUE);
        settings->put_IsBuiltInErrorPageEnabled(TRUE);
        settings->put_IsZoomControlEnabled(TRUE);
        ComPtr<ICoreWebView2Settings3> s3;
        if (SUCCEEDED(settings.As(&s3)) && s3) s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    }

    EventRegistrationToken token;

    web->add_DocumentTitleChanged(
        handler<ICoreWebView2DocumentTitleChangedEventHandler>(
            std::function<void(ICoreWebView2*, IUnknown*)>([this, tab](ICoreWebView2* sender, IUnknown*) {
                if (!alive(this) || !tab->webview) return;
                LPWSTR title = nullptr;
                if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                    pageTitleChanged(tab, title);
                    CoTaskMemFree(title);
                }
            }))
            .Get(),
        &token);

    web->add_SourceChanged(
        handler<ICoreWebView2SourceChangedEventHandler>(
            std::function<void(ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*)>(
                [this, tab](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) {
                    if (!alive(this) || !tab->webview) return;
                    LPWSTR url = nullptr;
                    if (SUCCEEDED(sender->get_Source(&url)) && url) {
                        pageURLChanged(tab, url);
                        CoTaskMemFree(url);
                    }
                }))
            .Get(),
        &token);

    web->add_NavigationStarting(
        handler<ICoreWebView2NavigationStartingEventHandler>(
            std::function<void(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*)>(
                [this, tab](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) {
                    if (!alive(this) || !tab->webview) return;
                    pageLoading(tab, true);
                }))
            .Get(),
        &token);

    web->add_NavigationCompleted(
        handler<ICoreWebView2NavigationCompletedEventHandler>(
            std::function<void(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*)>(
                [this, tab](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) {
                    if (!alive(this) || !tab->webview) return;
                    pageLoading(tab, false);
                    BOOL success = TRUE;
                    args->get_IsSuccess(&success);
                    tab->crashed = !success;
                    LPWSTR url = nullptr;
                    if (SUCCEEDED(sender->get_Source(&url)) && url) {
                        pageURLChanged(tab, url);
                        CoTaskMemFree(url);
                    }
                    BOOL back = FALSE, forward = FALSE;
                    sender->get_CanGoBack(&back);
                    sender->get_CanGoForward(&forward);
                    pageHistory(tab, back != FALSE, forward != FALSE);
                    if (success && !tab->isInternal()) history::note(tab->url, tab->shownTitle());
                }))
            .Get(),
        &token);

    web->add_HistoryChanged(
        handler<ICoreWebView2HistoryChangedEventHandler>(
            std::function<void(ICoreWebView2*, IUnknown*)>([this, tab](ICoreWebView2* sender, IUnknown*) {
                if (!alive(this) || !tab->webview) return;
                BOOL back = FALSE, forward = FALSE;
                sender->get_CanGoBack(&back);
                sender->get_CanGoForward(&forward);
                pageHistory(tab, back != FALSE, forward != FALSE);
            }))
            .Get(),
        &token);

    // A link that wants a new window gets a tab, as on the Mac.
    web->add_NewWindowRequested(
        handler<ICoreWebView2NewWindowRequestedEventHandler>(
            std::function<void(ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*)>(
                [this, tab](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
                    if (!alive(this)) return;
                    LPWSTR url = nullptr;
                    args->get_Uri(&url);
                    args->put_Handled(TRUE);
                    if (url) {
                        // A link clicked with Ctrl, or one a page opens by
                        // itself, comes to the row behind the page.
                        addTab(url, true);
                        CoTaskMemFree(url);
                    }
                }))
            .Get(),
        &token);

    // Shield: a request to an ad or tracker host is answered with nothing,
    // before it leaves the browser.
    web->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    web->add_WebResourceRequested(
        handler<ICoreWebView2WebResourceRequestedEventHandler>(
            std::function<void(ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*)>(
                [this, tab](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
                    if (!alive(this) || !tab) return;
                    ComPtr<ICoreWebView2WebResourceRequest> request;
                    args->get_Request(&request);
                    if (!request) return;
                    LPWSTR uri = nullptr;
                    request->get_Uri(&uri);
                    if (!uri) return;
                    std::wstring url = uri;
                    CoTaskMemFree(uri);
                    if (!shield::shouldBlock(url, tab->pageHost)) return;
                    auto env = wv2::environment();
                    if (!env) return;
                    ComPtr<ICoreWebView2WebResourceResponse> blocked;
                    env->CreateWebResourceResponse(nullptr, 403, L"Blocked", L"", &blocked);
                    if (blocked) args->put_Response(blocked.Get());
                }))
            .Get(),
        &token);

    // A page that crashes says so, and can be reloaded.
    web->add_ProcessFailed(
        handler<ICoreWebView2ProcessFailedEventHandler>(
            std::function<void(ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*)>(
                [this, tab](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) {
                    if (!alive(this)) return;
                    pageCrashed(tab);
                }))
            .Get(),
        &token);

    // The app's own pages: what their switches ask for comes back here.
    web->add_WebMessageReceived(
        handler<ICoreWebView2WebMessageReceivedEventHandler>(
            std::function<void(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)>(
                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
                    if (!alive(this)) return;
                    LPWSTR text = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text) {
                        runInternalAction(text);
                        CoTaskMemFree(text);
                    }
                }))
            .Get(),
        &token);

    // The page has the keys, so the app's own shortcuts are taken before the
    // page sees them: Ctrl+L, Ctrl+T, Ctrl+W, Ctrl+Tab and the rest.
    ComPtr<ICoreWebView2Controller> controller = tab->controller;
    controller->add_AcceleratorKeyPressed(
        handler<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            std::function<void(ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*)>(
                [this, tab](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
                    if (!alive(this)) return;
                    COREWEBVIEW2_KEY_EVENT_KIND kind;
                    args->get_KeyEventKind(&kind);
                    if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                        kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                        return;
                    }
                    UINT vk = 0;
                    args->get_VirtualKey(&vk);
                    // The page keeps Tab, Escape, the arrows and the rest.
                    bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                    if (control && (vk == 'L' || vk == 'T' || vk == 'W' || vk == 'R' || vk == 'D' ||
                                    vk == 'H' || vk == 'J' || vk == ',' || vk == VK_TAB || vk == VK_PRIOR ||
                                    vk == VK_NEXT || vk == 'N' || vk == 'Q' || vk == 'E' ||
                                    (vk >= '1' && vk <= '9'))) {
                        args->put_Handled(TRUE);
                        // Out of the page's callback: changing the tab in here
                        // would tear down the controller that is calling.
                        PostMessageW(hwnd_, WM_APP + 2, (WPARAM)vk,
                                     (shift ? 1 : 0) | (alt ? 2 : 0));
                    }
                }))
            .Get(),
        &token);

    attach(tab);

    if (tab->isInternal()) {
        tab->webview->NavigateToString(internalHTML(tab->internal).c_str());
        return;
    }

    // Before the page loads: the stylesheet that hides the ad slots, and
    // YouTube's own ad schedule taken out of its data.
    std::wstring css = shield::styleFor(tab->pageHost);
    if (!css.empty()) {
        std::wstring script = L"(function(){var s=document.createElement('style');s.textContent=" +
                              wv2::jsString(css) +
                              L";(document.head||document.documentElement).appendChild(s);})();";
        tab->webview->AddScriptToExecuteOnDocumentCreated(script.c_str(), nullptr);
    }
    std::wstring youtube = shield::youtubeScript(tab->pageHost);
    if (!youtube.empty()) {
        tab->webview->AddScriptToExecuteOnDocumentCreated(youtube.c_str(), nullptr);
    }
    if (!tab->url.empty()) tab->webview->Navigate(tab->url.c_str());
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
        if (tab->webview) {
            tab->webview->NavigateToString(internalHTML(tab->internal).c_str());
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
    if (!tab->controller) {
        makeWebView(tab);
    } else if (tab->webview) {
        tab->webview->Navigate(url.c_str());
    }
    updateTitle();
    relayout();
}

void BrowserWindow::goBack() {
    if (Tab* tab = active(); tab && tab->webview) tab->webview->GoBack();
}

void BrowserWindow::goForward() {
    if (Tab* tab = active(); tab && tab->webview) tab->webview->GoForward();
}

void BrowserWindow::reload() {
    Tab* tab = active();
    if (!tab) return;
    if (tab->isInternal()) {
        if (tab->webview) tab->webview->NavigateToString(internalHTML(tab->internal).c_str());
        return;
    }
    if (!tab->webview) {
        makeWebView(tab);
        return;
    }
    tab->webview->Reload();
}

void BrowserWindow::stop() {
    if (Tab* tab = active(); tab && tab->webview) tab->webview->Stop();
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
    if (tab->webview) {
        ComPtr<ICoreWebView2_8> web8;
        if (SUCCEEDED(tab->webview.As(&web8)) && web8) {
            web8->put_IsMuted(tab->muted ? TRUE : FALSE);
        }
    }
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
// WebView2's own surface, which Windows composites above everything the window
// draws, and which neither a region nor a window of the app's own can be made
// to cover. So while the field is up, the page steps down below it (see
// layout()), and comes back up when the field goes.
void BrowserWindow::paintSheet() { relayout(); }
