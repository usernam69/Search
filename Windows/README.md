# Search for Windows

The Mac app's design and behaviour, built again natively for Windows: the same
column of tabs, the same card for the page, the same address field, the same
colours and measurements, the same rules for what a typed address means, the
same Shield lists. It is C++20 with Win32, GDI+ and Microsoft WebView2 — the
Edge engine Windows already has — so there is no runtime to install and nothing
of Apple's to carry over.

## Build

Needs:

- Visual Studio 2022 Build Tools with the C++ workload (MSVC 14.4x, Windows
  SDK 10.0.26100 or newer).
- PowerShell and `curl`, which `build.bat` uses to fetch the WebView2 SDK.

```bat
cd Windows
build.bat
```

It downloads the WebView2 NuGet package into `build\webview2` on first use,
compiles the fourteen sources in `src`, and writes `build\Search.exe`. Add
`clean` to remove `build\obj` first.

`Search.exe` needs the WebView2 Runtime, which is part of Windows 11 and comes
with Microsoft Edge on Windows 10. It is started from the Start menu or from a
link; a second launch brings the first window forward instead of opening a
second one.

## What it keeps

Everything the Mac app decides in its own code, rather than handing to WebKit
or AppKit, is the same here:

| On the Mac | Here |
| --- | --- |
| `Design.swift`'s palette and metrics | `src/Theme.cpp` — the same light/dark pairs, the same 52-point band, 232-point column, 560-point field |
| `Address.swift`'s rules | `src/Address.cpp` — localhost, `.local` and private ranges get `http`, everything else `https`; an email is words; `[::1]` and ports are kept |
| `Engine.swift`'s search templates | `src/App.cpp` |
| Shield's hosts, selectors and YouTube scripts | `src/Shield.cpp` — 46 hosts, the same slots, the same 16× muted skipper |
| History, bookmarks, the session, closed tabs, paused sites | `src/Data.cpp` — the same JSON shapes in `%LOCALAPPDATA%\Search`, so a copy of the data from either machine reads the same |
| Settings | `HKCU\Software\Search`, which is what Windows has instead of `UserDefaults` |
| The app's own pages | `src/Internal.cpp` — New Tab, Settings, History, Bookmarks, Downloads, in the palette's own colours |

Ad and tracker blocking is a WebView2 resource filter answering 403 rather than
WebKit's content rule lists; the effect is the same, and first-party requests
are never blocked.

## Where it differs, and why

- **The address field steps the page down.** WebView2 draws with
  DirectComposition, which Windows composites above everything a window paints:
  a region cut out of the page's window, a layered window over it, and a
  topmost popup all fail to cover it. So while the field is up, the page is
  laid out below it and comes back when the field closes. The field and its
  suggestions are drawn by the window itself, as on the Mac.
- **No caption.** `WM_NCCALCSIZE` takes the title bar away, the band the app
  paints is the drag area, and minimise, maximise and close are drawn at the
  top right as Windows draws them.
- **Text is grayscale on the field's own pass.** ClearType writes no alpha, and
  the window paints into a 32-bit section and hands it over in one go.

## Not here yet

Downloads (the page lists them, nothing starts them), private tabs, the
bookmarks bar, reader mode, split view, extensions, and the AI panel. Each is
on the Mac side and none of them is stubbed out here.

## Files

```
Windows/
  build.bat          fetch WebView2, compile, link
  make-icon.py       draw src/search.ico from the Mac's own icon path
  src/Address.*      what a typed address means
  src/Com.h          the WRL handler the callbacks are made with
  src/App.*          the engines, the settings, the app's windows
  src/Data.*         history, bookmarks, the session, closed tabs
  src/Input.cpp      the pointer and the keys
  src/Internal.*     the app's own pages
  src/Json.*         a small JSON reader and writer
  src/Main.cpp       the entry point, the theme, the title, the app's actions
  src/Paint.cpp      layout and drawing
  src/Shield.*       the block lists, the slots, the YouTube scripts
  src/Tab.h          a tab and what it shows
  src/Store.*        the folder, the files, the settings
  src/Tabs.cpp       tabs and their web views
  src/Theme.*        colours, metrics, fonts, drawing helpers
  src/WebView.*      the WebView2 environment
  src/Window.*       the window and its messages
  src/Search.manifest, Search.rc
```
