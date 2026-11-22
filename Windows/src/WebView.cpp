#include "WebView.h"

#include <wrl/event.h>

#include <deque>

#include "Com.h"
#include "Store.h"
#include "Theme.h"
#include "WebView2EnvironmentOptions.h"

using namespace Microsoft::WRL;

namespace wv2 {

static ComPtr<ICoreWebView2Environment> g_env;
static bool g_started = false;
static bool g_ready = false;
static bool g_failed = false;
static std::deque<Ready> g_waiting;

class EnvHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                       ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* environment) override {
        if (FAILED(result) || !environment) {
            wchar_t msg[256];
            swprintf(msg, 256, L"environment callback failed: 0x%08lx", (unsigned long)result);
            store::log(msg);
            g_failed = true;
        } else {
            g_env = environment;
            g_ready = true;
        }
        auto waiting = g_waiting;
        g_waiting.clear();
        for (auto& done : waiting) done(g_ready);
        return S_OK;
    }
};

std::wstring userDataFolder() {
    std::wstring folder = store::folder() + L"\\WebView2";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

// The WebView2 runtime is shipped in `runtime` beside Search.exe, so the app
// never depends on the Edge or WebView2 runtime installed on the PC.
static std::wstring runtimeFolder() {
    wchar_t path[MAX_PATH] = {};
    DWORD used = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring exe(path, used ? used : 0);
    size_t slash = exe.find_last_of(L"\\/");
    std::wstring base = slash == std::wstring::npos ? L"." : exe.substr(0, slash);
    std::wstring folder = base + L"\\runtime";

    if (GetFileAttributesW((folder + L"\\msedgewebview2.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t msg[512];
        swprintf(msg, 512, L"Bundled WebView2 runtime is missing: %ls", folder.c_str());
        store::log(msg);
    }
    return folder;
}

void ensureEnvironment(Ready done) {
    if (g_ready) {
        done(true);
        return;
    }
    if (g_failed) {
        done(false);
        return;
    }
    g_waiting.push_back(std::move(done));
    if (g_started) return;
    g_started = true;

    // The profile lives in the app's own folder.
    auto options = Make<CoreWebView2EnvironmentOptions>();
    // Nothing but the app's own pages and the sites you ask for: no
    // Microsoft telemetry from the browser itself.
    options->put_AdditionalBrowserArguments(L"--disable-features=msEdgeSplashScreen --no-first-run");
    std::wstring folder = userDataFolder();
    std::wstring runtime = runtimeFolder();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(runtime.c_str(), folder.c_str(), options.Get(),
                                                          Make<EnvHandler>().Get());
    if (FAILED(hr)) {
        wchar_t msg[512];
        swprintf(msg, 512,
                 L"CreateCoreWebView2EnvironmentWithOptions failed: 0x%08lx runtime=%ls folder=%ls",
                 (unsigned long)hr, runtime.c_str(), folder.c_str());
        store::log(msg);
        g_failed = true;
        auto waiting = g_waiting;
        g_waiting.clear();
        for (auto& cb : waiting) cb(false);
    }
}

ComPtr<ICoreWebView2Environment> environment() { return g_env; }

std::wstring jsString(const std::wstring& text) {
    std::wstring out = L"\"";
    for (wchar_t c : text) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            case L'<': out += L"\\u003c"; break;
            case L'>': out += L"\\u003e"; break;
            case L'&': out += L"\\u0026"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += L'"';
    return out;
}

}  // namespace wv2
