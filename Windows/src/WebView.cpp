// The browser engine: CEF is hosted in this process, with a native child
// window per tab. The rest of the app only knows the callbacks in WebView.h.
#include "WebView.h"

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_base.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_cookie.h"
#include "include/cef_frame.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_process_message.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_request.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_scheme.h"
#include "include/cef_stream.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_stream_resource_handler.h"

#include "App.h"
#include "Internal.h"
#include "Shield.h"
#include "Store.h"
#include "Window.h"

namespace engine {

namespace {

constexpr UINT kPumpMessage = WM_APP + 0x57;
constexpr int kTimerId = 0x5657;
constexpr int kPumpIntervalMs = 33;

struct Callbacks {
    std::function<void(Tab*)> created;
    std::function<void(Tab*)> attached;
    std::function<void(Tab*, const std::wstring&)> title;
    std::function<void(Tab*, const std::wstring&)> url;
    std::function<void(Tab*, bool)> loading;
    std::function<void(Tab*, bool, bool)> history;
    std::function<void(Tab*)> crashed;
    std::function<void(const std::wstring&)> action;
};

struct TabState {
    Tab* tab = nullptr;
    CefRefPtr<CefBrowser> browser;
    HWND hwnd = nullptr;
    bool attached = false;
    bool closing = false;
    std::wstring pendingURL;
    std::wstring pendingHTML;
    bool pendingLoad = false;
    RECT page{};
    std::vector<RECT> caption;
    int radius = 0;
    bool visible = true;
    bool focused = false;
};

std::atomic<bool> g_initialized{false};
std::atomic<bool> g_shuttingDown{false};
std::atomic<bool> g_shutdownRequested{false};
std::atomic<bool> g_pumping{false};
std::atomic<int> g_exitCode{0};
std::atomic<int> g_tabsCreated{0};
HINSTANCE g_instance = nullptr;
HWND g_pumpWindow = nullptr;
CefRefPtr<CefApp> g_app;
CefRefPtr<CefCookieManager> g_cookies;
std::unique_ptr<CefScopedSandboxInfo> g_sandbox;
std::mutex g_mutex;
std::map<int, std::shared_ptr<TabState>> g_tabs;
std::set<int> g_closePending;
Callbacks g_callbacks;

bool uiThread() { return GetCurrentThreadId() == app::uiThread(); }

std::wstring wide(const CefString& text) {
    if (text.empty()) return {};
    const char16_t* p = text.c_str();
    return std::wstring(reinterpret_cast<const wchar_t*>(p), wcslen(reinterpret_cast<const wchar_t*>(p)));
}

std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.data(), n);
    return out;
}

CefString cef(const std::wstring& text) { return CefString(text.c_str()); }

std::shared_ptr<TabState> stateFor(CefBrowser* browser) {
    if (!browser) return {};
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_tabs.find(browser->GetIdentifier());
    return it == g_tabs.end() ? std::shared_ptr<TabState>() : it->second;
}

std::shared_ptr<TabState> stateFor(Tab* tab) {
    if (!tab) return {};
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& entry : g_tabs)
        if (entry.second->tab == tab) return entry.second;
    return {};
}

bool liveTab(Tab* tab) {
    if (!tab) return false;
    auto windows = liveWindows();
    return windows.count(tab->window) > 0;
}

void callAction(const std::wstring& action) {
    if (g_callbacks.action) g_callbacks.action(action);
}

void applyBounds(const std::shared_ptr<TabState>& state) {
    if (!state || !state->browser || !state->browser->GetHost()) return;
    CefWindowHandle host = state->browser->GetHost()->GetWindowHandle();
    if (!host) return;
    SetWindowPos(host, nullptr, state->page.left, state->page.top,
                 state->page.right - state->page.left, state->page.bottom - state->page.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    HRGN region = CreateRectRgnIndirect(&state->page);
    for (const RECT& cut : state->caption) {
        HRGN hole = CreateRectRgn(cut.left, cut.top, cut.right, cut.bottom);
        CombineRgn(region, region, hole, RGN_DIFF);
        DeleteObject(hole);
    }
    if (state->radius > 0) {
        RECT rounded = state->page;
        HRGN round = CreateRoundRectRgn(rounded.left, rounded.top, rounded.right + 1, rounded.bottom + 1,
                                        state->radius, state->radius);
        HRGN corners = CreateRectRgnIndirect(&state->page);
        CombineRgn(corners, corners, round, RGN_DIFF);
        CombineRgn(region, region, corners, RGN_DIFF);
        DeleteObject(corners);
        DeleteObject(round);
    }
    SetWindowRgn(host, region, TRUE);
}

void applyVisibility(const std::shared_ptr<TabState>& state) {
    if (!state || !state->browser || !state->browser->GetHost()) return;
    HWND host = state->browser->GetHost()->GetWindowHandle();
    if (!host) return;
    ShowWindow(host, state->visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (state->visible && state->focused) SetFocus(host);
}

void flushPending(const std::shared_ptr<TabState>& state) {
    if (!state || !state->browser || !state->browser->GetMainFrame()) return;
    if (state->pendingLoad) {
        if (!state->pendingHTML.empty())
            state->browser->GetMainFrame()->LoadString(cef(state->pendingHTML), CefString());
        else
            state->browser->GetMainFrame()->LoadURL(cef(state->pendingURL));
        state->pendingLoad = false;
        state->pendingURL.clear();
        state->pendingHTML.clear();
    }
}

LRESULT CALLBACK pumpWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case kPumpMessage:
        case WM_TIMER:
            pump();
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

void schedulePump(int delayMs) {
    if (!g_pumpWindow) return;
    if (delayMs < 0) delayMs = 0;
    KillTimer(g_pumpWindow, kTimerId);
    SetTimer(g_pumpWindow, kTimerId, (UINT)delayMs, nullptr);
}

int exitWhenClosed() {
    int code = g_exitCode.load();
    if (g_shutdownRequested && g_tabs.empty()) PostQuitMessage(code);
    return code;
}

// ---- app, browser process ----

class App : public CefApp,
            public CefBrowserProcessHandler,
            public CefRenderProcessHandler {
public:
    App() = default;

    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
        // Internal pages are a standard, local scheme in every process.
        registrar->AddCustomScheme("search", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE);
    }

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

    void OnContextInitialized() override {
        CefRegisterSchemeHandlerFactory(
            "search", "", new SearchSchemeFactory());
    }

    void OnScheduleMessagePumpWork(int64_t delayMs) override { schedulePump((int)delayMs); }

    void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override {
        if (!frame->IsMain() || !context) return;
        CefRefPtr<CefV8Value> chrome = context->GetGlobal()->GetValue("chrome");
        if (!chrome || !chrome->IsObject()) {
            chrome = CefV8Value::CreateObject(nullptr);
            context->GetGlobal()->SetValue("chrome", chrome, V8_PROPERTY_ATTRIBUTE_NONE);
        }
        CefRefPtr<CefV8Value> webview = chrome->GetValue("webview");
        if (!webview || !webview->IsObject()) {
            webview = CefV8Value::CreateObject(nullptr);
            chrome->SetValue("webview", webview, V8_PROPERTY_ATTRIBUTE_NONE);
        }
        webview->SetValue("postMessage",
                          CefV8Value::CreateFunction("postMessage", new Bridge(browser)),
                          V8_PROPERTY_ATTRIBUTE_NONE);
    }

private:
    class Bridge : public CefV8Handler {
    public:
        explicit Bridge(CefRefPtr<CefBrowser> browser) : browser_(browser) {}
        bool Execute(const CefString& name, CefRefPtr<CefV8Value> object,
                     const CefV8ValueList& args, CefRefPtr<CefV8Value>& retval,
                     CefString& exception) override {
            if (args.empty()) return true;
            std::wstring action;
            if (args[0]->IsString()) action = wide(args[0]->GetStringValue());
            else action = wide(args[0]->ToString()->GetStringValue());
            CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("search.postMessage");
            message->GetArgumentList()->SetString(0, cef(action));
            if (browser_ && browser_->GetMainFrame())
                browser_->GetMainFrame()->SendProcessMessage(PID_BROWSER, message);
            return true;
        }
        CefRefPtr<CefBrowser> browser_;
        IMPLEMENT_REFCOUNTING(Bridge);
    };

    class SearchSchemeHandler : public CefResourceHandler {
    public:
        explicit SearchSchemeHandler(std::wstring html) : data_(std::move(html)) {}

        bool ProcessRequest(CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) override {
            callback->Continue();
            return true;
        }
        void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t& responseLength,
                                CefString& redirectUrl) override {
            response->SetStatus(200);
            response->SetMimeType("text/html; charset=utf-8");
            responseLength = (int64_t)data_.size() * sizeof(wchar_t);
        }
        bool ReadResponse(void* dataOut, int bytesToRead, int& bytesRead,
                          CefRefPtr<CefCallback> callback) override {
            if (offset_ >= data_.size()) {
                bytesRead = 0;
                return false;
            }
            int bytes = (int)(data_.size() - offset_) * sizeof(wchar_t);
            if (bytes > bytesToRead) bytes = bytesToRead;
            memcpy(dataOut, (const wchar_t*)data_.data() + offset_, (size_t)bytes);
            offset_ += bytes / sizeof(wchar_t);
            bytesRead = bytes;
            return true;
        }
        void Cancel() override {}

    private:
        std::wstring data_;
        size_t offset_ = 0;
        IMPLEMENT_REFCOUNTING(SearchSchemeHandler);
    };

    class SearchSchemeFactory : public CefSchemeHandlerFactory {
    public:
        CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             const CefString& schemeName,
                                             CefRefPtr<CefRequest> request) override {
            return new SearchSchemeHandler(internalHTML(wide(request->GetURL())));
        }
        IMPLEMENT_REFCOUNTING(SearchSchemeFactory);
    };

    IMPLEMENT_REFCOUNTING(App);
};

class ShieldHandler : public CefResourceRequestHandler {
public:
    explicit ShieldHandler(int tabId) : tabId_(tabId) {}

    cef_return_value_t OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                             CefRefPtr<CefRequest> request,
                                             CefRefPtr<CefRequestCallback> callback) override {
        std::wstring url = wide(request->GetURL());
        if (shouldBlock(url)) {
            CefRefPtr<CefStreamReader> empty = CefStreamReader::CreateForData(nullptr, 0);
            request->SetURL("data:text/plain,");
            (void)empty;
        }
        return RV_CONTINUE;
    }

    CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser> browser,
                                                     CefRefPtr<CefFrame> frame,
                                                     CefRefPtr<CefRequest> request) override {
        if (!shouldBlock(wide(request->GetURL()))) return nullptr;
        std::string empty;
        return new CefStreamResourceHandler(200, "text/plain", CefResponse::HeaderMap(),
                                            CefStreamReader::CreateForData(
                                                (void*)empty.data(), 0));
    }

private:
    int tabId_;
    IMPLEMENT_REFCOUNTING(ShieldHandler);
};

class Client : public CefClient,
               public CefLifeSpanHandler,
               public CefLoadHandler,
               public CefRequestHandler,
               public CefDisplayHandler,
               public CefInputHandler {
public:
    Client() = default;

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefInputHandler> GetInputHandler() override { return this; }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
        if (message->GetName() == "search.postMessage" && source == PID_RENDERER) {
            callAction(wide(message->GetArgumentList()->GetString(0)));
            return true;
        }
        return false;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        if (!uiThread()) return;
        auto state = stateFor(browser);
        if (!state) return;
        state->browser = browser;
        state->hwnd = browser->GetHost()->GetWindowHandle();
        applyBounds(state);
        applyVisibility(state);
        flushPending(state);
        ++g_tabsCreated;
        if (g_callbacks.attached) g_callbacks.attached(state->tab);
    }

    bool DoClose(CefRefPtr<CefBrowser> browser) override {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_closePending.erase(browser->GetIdentifier());
        return false;
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        int id = browser->GetIdentifier();
        std::shared_ptr<TabState> state;
        bool empty = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_tabs.find(id);
            if (it != g_tabs.end()) {
                state = it->second;
                state->browser = nullptr;
                g_tabs.erase(it);
            }
            g_closePending.erase(id);
            empty = g_tabs.empty();
        }
        if (state && g_callbacks.crashed && g_callbacks.url && g_callbacks.title &&
            g_callbacks.loading && g_callbacks.history && uiThread()) {
            g_callbacks.title(state->tab, L"");
            g_callbacks.url(state->tab, L"");
            g_callbacks.loading(state->tab, false);
            g_callbacks.history(state->tab, false, false);
        }
        if (empty) exitWhenClosed();
    }

    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     CefTransitionType transitionType) override {
        if (!frame->IsMain()) return;
        auto state = stateFor(browser);
        if (!state || !liveTab(state->tab)) return;
        if (g_callbacks.loading) g_callbacks.loading(state->tab, true);
    }

    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override {
        if (!frame->IsMain()) return;
        auto state = stateFor(browser);
        if (!state || !liveTab(state->tab)) return;
        if (g_callbacks.loading) g_callbacks.loading(state->tab, false);
    }

    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefErrorCode errorCode,
                     const CefString& errorText, const CefString& failedUrl) override {
        if (!frame->IsMain()) return;
        auto state = stateFor(browser);
        if (!state || !liveTab(state->tab)) return;
        if (g_callbacks.loading) g_callbacks.loading(state->tab, false);
        if (g_callbacks.crashed) g_callbacks.crashed(state->tab);
    }

    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
        auto state = stateFor(browser);
        if (!state || !liveTab(state->tab)) return;
        if (g_callbacks.title) g_callbacks.title(state->tab, wide(title));
    }

    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         const CefString& url) override {
        if (!frame->IsMain()) return;
        auto state = stateFor(browser);
        if (!state || !liveTab(state->tab)) return;
        std::wstring text = wide(url);
        if (g_callbacks.url) g_callbacks.url(state->tab, text);
        CefRefPtr<CefBrowserHost> host = browser->GetHost();
        if (host && g_callbacks.history)
            g_callbacks.history(state->tab, host->CanGoBack(), host->CanGoForward());
    }

    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request, bool isNavigation, bool isDownload,
        const CefString& requestInitiator, bool* disableDefaultHandling) override {
        if (isNavigation || !request || !isInternalURL(wide(request->GetURL()))) return nullptr;
        return new ShieldHandler(browser ? browser->GetIdentifier() : 0);
    }

    bool OnKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                    CefEventHandle osEvent) override {
        const MSG* msg = (const MSG*)osEvent;
        if (!msg || msg->message != WM_KEYDOWN) return false;
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        bool shift = GetKeyState(VK_SHIFT) < 0;
        bool alt = GetKeyState(VK_MENU) < 0;
        if (!ctrl) return false;
        switch (msg->wParam) {
            case 'L': case 'T': case 'W': case 'R': case 'D': case 'H': case 'J':
            case ',': case VK_TAB: case VK_PRIOR: case VK_NEXT: case 'N': case 'Q':
            case 'E': case '1': case '2': case '3': case '4': case '5': case '6':
            case '7': case '8': case '9':
                if (g_callbacks.action) {
                    std::wstring action = L"key:" + std::to_wstring((unsigned)msg->wParam);
                    if (shift) action += L":shift";
                    if (alt) action += L":alt";
                    g_callbacks.action(action);
                }
                *const_cast<bool*>(&event.is_keyboard_shortcut) = true;
                return true;
            default:
                return false;
        }
    }

private:
    IMPLEMENT_REFCOUNTING(Client);
};

}  // namespace

int preMain(HINSTANCE instance) {
    g_instance = instance;
    void* sandboxInfo = nullptr;
#if defined(CEF_USE_SANDBOX)
    g_sandbox = std::make_unique<CefScopedSandboxInfo>();
    sandboxInfo = g_sandbox->sandbox_info();
#endif
    CefMainArgs args(instance);
    g_app = new App;
    return CefExecuteProcess(args, g_app, sandboxInfo);
}

bool initialize(HINSTANCE, const Callbacks& callbacks) {
    if (g_initialized) return true;
    g_callbacks = callbacks;

    WNDCLASSW wc{};
    wc.lpfnWndProc = pumpWndProc;
    wc.hInstance = g_instance;
    wc.lpszClassName = L"SearchCefPump";
    RegisterClassW(&wc);
    g_pumpWindow = CreateWindowW(L"SearchCefPump", nullptr, 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, g_instance, nullptr);
    if (!g_pumpWindow) {
        store::log(L"CEF pump window failed");
        return false;
    }

    CefSettings settings;
    CefString(&settings.browser_subprocess_path) = cef(modulePath());
    CefString(&settings.root_cache_path) = cef(userDataFolder());
    CefString(&settings.cache_path) = cef(userDataFolder() + L"\\Cache");
    CefString(&settings.log_file) = cef(store::file(L"cef.log"));
    settings.external_message_pump = 1;
    settings.multi_threaded_message_loop = 0;
    settings.windowless_rendering_enabled = 0;
    settings.no_sandbox = true;

    void* sandboxInfo = nullptr;
#if defined(CEF_USE_SANDBOX)
    sandboxInfo = g_sandbox ? g_sandbox->sandbox_info() : nullptr;
#endif
    CefMainArgs args(g_instance);
    bool ok = CefInitialize(args, settings, g_app, sandboxInfo);
    if (ok) {
        g_initialized = true;
        g_cookies = CefCookieManager::GetGlobalManager(nullptr);
        schedulePump(kPumpIntervalMs);
    } else {
        store::log(L"CEF initialize failed");
    }
    return ok;
}

void shutdown() {
    if (g_shuttingDown || !g_initialized) return;
    g_shuttingDown = true;
    g_shutdownRequested = true;

    std::vector<CefRefPtr<CefBrowser>> browsers;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& entry : g_tabs) {
            if (entry.second->browser) browsers.push_back(entry.second->browser);
            g_closePending.insert(entry.first);
        }
    }
    if (browsers.empty()) {
        g_initialized = false;
        CefShutdown();
        if (g_pumpWindow) {
            DestroyWindow(g_pumpWindow);
            g_pumpWindow = nullptr;
        }
        g_app = nullptr;
        g_sandbox.reset();
        PostQuitMessage(g_exitCode.load());
        return;
    }
    for (auto& browser : browsers) {
        CefRefPtr<CefBrowserHost> host = browser->GetHost();
        if (host) host->TryCloseBrowser();
    }
}

void pump() {
    if (!g_initialized || g_shuttingDown && g_tabs.empty()) return;
    bool expected = false;
    if (!g_pumping.compare_exchange_strong(expected, true)) return;
    CefDoMessageLoopWork();
    g_pumping = false;
}

int exitCode() { return exitWhenClosed(); }

std::wstring userDataFolder() { return store::folder() + L"\\CEF"; }

std::wstring jsString(const std::wstring& text) {
    std::wstring out = L"'";
    for (wchar_t c : text) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'\'': out += L"\\'"; break;
            case L'\r': break;
            case L'\n': out += L"\\n"; break;
            default: out += c;
        }
    }
    out += L"'";
    return out;
}

void create(Tab* tab) {
    if (!tab || !g_initialized) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& entry : g_tabs)
        if (entry.second->tab == tab) return;
    auto state = std::make_shared<TabState>();
    state->tab = tab;
    g_tabs[++g_tabsCreated] = state;
    if (g_callbacks.created) g_callbacks.created(tab);
}

bool has(Tab* tab) {
    auto state = stateFor(tab);
    return state && state->browser && !state->closing;
}

void destroy(Tab* tab) {
    auto state = stateFor(tab);
    if (!state) return;
    CefRefPtr<CefBrowser> browser = state->browser;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (browser) g_closePending.insert(browser->GetIdentifier());
    }
    if (browser && browser->GetHost()) browser->GetHost()->TryCloseBrowser();
    else {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto it = g_tabs.begin(); it != g_tabs.end();) {
            if (it->second == state) it = g_tabs.erase(it);
            else ++it;
        }
    }
}

void setVisible(Tab* tab, bool visible) {
    auto state = stateFor(tab);
    if (!state) return;
    state->visible = visible;
    applyVisibility(state);
}

void setBounds(Tab* tab, const RECT& page, const std::vector<RECT>& caption, int radius) {
    auto state = stateFor(tab);
    if (!state) return;
    state->page = page;
    state->caption = caption;
    state->radius = radius;
    applyBounds(state);
}

void focus(Tab* tab) {
    auto state = stateFor(tab);
    if (!state) return;
    state->focused = true;
    applyVisibility(state);
    if (state->browser && state->browser->GetHost()) state->browser->GetHost()->SetFocus(true);
}

void navigate(Tab* tab, const std::wstring& url) {
    auto state = stateFor(tab);
    if (!state) return;
    if (!state->browser) {
        state->pendingURL = url;
        state->pendingHTML.clear();
        state->pendingLoad = true;
        return;
    }
    CefRefPtr<CefFrame> frame = state->browser->GetMainFrame();
    if (frame) frame->LoadURL(cef(url));
}

void loadHTML(Tab* tab, const std::wstring& html) {
    auto state = stateFor(tab);
    if (!state) return;
    if (!state->browser) {
        state->pendingHTML = html;
        state->pendingURL.clear();
        state->pendingLoad = true;
        return;
    }
    CefRefPtr<CefFrame> frame = state->browser->GetMainFrame();
    if (frame) frame->LoadString(cef(html), CefString());
}

void goBack(Tab* tab) {
    auto state = stateFor(tab);
    if (state && state->browser && state->browser->GetHost()) state->browser->GetHost()->GoBack();
}

void goForward(Tab* tab) {
    auto state = stateFor(tab);
    if (state && state->browser && state->browser->GetHost()) state->browser->GetHost()->GoForward();
}

void reload(Tab* tab) {
    auto state = stateFor(tab);
    if (state && state->browser && state->browser->GetHost()) state->browser->GetHost()->Reload();
}

void stop(Tab* tab) {
    auto state = stateFor(tab);
    if (state && state->browser && state->browser->GetHost()) state->browser->GetHost()->StopLoad();
}

void setMuted(Tab* tab, bool muted) {
    auto state = stateFor(tab);
    if (state && state->browser && state->browser->GetHost()) state->browser->GetHost()->SetAudioMuted(muted);
}

void markClosed(Tab* tab) {
    auto state = stateFor(tab);
    if (!state) return;
    state->closing = true;
}

}  // namespace engine
