// A tab: one page, its web view, and what the row says about it.
//
// A tab restored from last session costs nothing until it is picked: it is a
// row and an address, and the page is made when it is first shown. That is
// most of why launching with twenty tabs is still instant, as on the Mac.
#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

#include <string>

class BrowserWindow;

struct Tab {
    BrowserWindow* window = nullptr;
    int id = 0;

    std::wstring url;      // the page on screen, empty until it has one
    std::wstring title;    // the page's own
    std::wstring name;     // the name you gave the tab, when you gave it one
    std::wstring pin;      // the letter a pinned tab shows; empty if not pinned
    std::wstring home;     // a pin's own page, the one it was pinned at
    std::wstring internal; // "settings", "history"… for one of the app's pages

    bool loading = false;
    bool canGoBack = false;
    bool canGoForward = false;
    bool muted = false;
    bool asleep = false;   // restored, not yet woken: no web view yet
    bool crashed = false;
    bool audible = false;

    std::wstring pageHost;

    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;

    std::wstring shownTitle() const;
    std::wstring shownURL() const;
    bool isPinned() const { return !pin.empty(); }
    bool isInternal() const { return !internal.empty(); }
};
