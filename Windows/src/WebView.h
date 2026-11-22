// The environment every tab's web view is made in: the WebView2 runtime
// bundled beside the app, its profile kept in the app's own folder, and the
// loader that lets a tab ask for one.
#pragma once

#include <windows.h>
#include <wrl.h>

#include <functional>
#include <string>
#include <WebView2.h>

namespace wv2 {

// The environment, made once. `done` runs on the UI thread when it is ready,
// with whether it worked.
using Ready = std::function<void(bool ok)>;
void ensureEnvironment(Ready done);

Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment();

// The user data folder, beside the app's own files.
std::wstring userDataFolder();

// A string as a JavaScript literal, quotes and all.
std::wstring jsString(const std::wstring& text);

}  // namespace wv2
