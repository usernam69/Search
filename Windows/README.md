# Search for Windows

A small, fast, quiet web browser: a column of tabs down the left, the page as a
card beside it, and one field for an address or a few words. It is C++20 with
Win32 and GDI+ for the app, and its own bundled WebView2 runtime for pages — no
browser, runtime, or installer has to be present on the PC.

## Build

Needs:

- Visual Studio 2022 Build Tools with the C++ workload (MSVC 14.4x, Windows
  SDK 10.0.26100 or newer).
- PowerShell, which `build.bat` uses to fetch the WebView2 SDK and runtime.
  The fixed-version runtime is a ~418 MB download that expands to ~945 MB, and
  is fetched only once.

```bat
cd Windows
build.bat
```

It downloads the WebView2 SDK into `build\webview2` and the fixed-version
runtime into `build\runtime` on first use, compiles the fourteen sources in
`src`, and writes `build\Search.exe`. Ship `Search.exe` together with the whole
`runtime` folder; the app finds it beside itself and does not look at the
machine's Edge or WebView2 install. A second launch brings the first window
forward instead of opening a second one.

The runtime is fixed, so it does not update itself. A new Search build carries
a new runtime; ship that folder with the update to pick up browser fixes.

## What it does

- **One field.** Type an address and you go there; type words and you search.
  Local names — `localhost`, `.local`, private ranges — get `http`; everything
  else `https`. Suggestions come from your history, bookmarks and open tabs,
  and nothing leaves the machine until you press Return.
- **Tabs that stay out of the way.** A column of rows, each the page's own
  title. Pinned pages shrink to a letter at the top. Tabs from your last
  session come back instantly and cost nothing until you pick them.
- **Shield**, the ad and tracker blocker, on by default: requests to 46 known
  ad and tracker hosts are answered with 403 before they leave the browser, a
  stylesheet hides the empty boxes ads leave behind, and YouTube's ad schedule
  is taken out of the player's own data. Off per site from Settings.
- **The app's own pages.** New Tab, Settings, History, Bookmarks and Downloads
  are pages in a tab, drawn in the app's own colours.
- **Your data stays on this PC.** History, bookmarks, the session, closed tabs
  and paused sites are JSON files in `%LOCALAPPDATA%\Search`; settings live in
  `HKCU\Software\Search`.

### Shortcuts

| Keys | What |
| --- | --- |
| `Ctrl+L`, `F6`, `Ctrl+E` | the address field |
| `Ctrl+T` / `Ctrl+Shift+T` | a new tab / the tab you just closed |
| `Ctrl+W` / `Ctrl+Shift+W` | close the tab / the window |
| `Ctrl+Tab`, `Ctrl+PageDown` | the next tab (`Shift` for the one before) |
| `Ctrl+1`–`9` | a tab by its place |
| `Ctrl+R`, `F5` | reload |
| `Alt+←` / `Alt+→` | back / forward |
| `Ctrl+D` | bookmark this page |
| `Ctrl+H` / `Ctrl+J` / `Ctrl+,` | History / Downloads / Settings |
| `Ctrl+N` / `Ctrl+Shift+N` | a new window |
| `Ctrl+Q` | quit |
| `Esc` | close the field |

## How it is laid out

| Part | Where |
| --- | --- |
| Colours, metrics, fonts, drawing helpers | `src/Theme.*` |
| What a typed address means | `src/Address.*` |
| The search engines and the app's windows | `src/App.*` |
| History, bookmarks, the session, closed tabs | `src/Data.*` |
| The app's own pages | `src/Internal.*` |
| Shield's hosts, selectors and YouTube scripts | `src/Shield.*` |
| Layout and drawing | `src/Paint.cpp` |
| The pointer and the keys | `src/Input.cpp` |
| Tabs and their web views | `src/Tabs.cpp`, `src/Tab.h` |
| The window and its messages | `src/Window.*` |
| The WebView2 environment | `src/WebView.*` |
| The folder, the files, the settings | `src/Store.*` |
| A small JSON reader and writer | `src/Json.*` |
| The entry point, the theme, the title | `src/Main.cpp` |
| The WRL handler the callbacks are made with | `src/Com.h` |
| The icon, drawn from the logomark's own path | `make-icon.py` |

## Two things Windows makes different

- **The address field steps the page down.** WebView2 draws with
  DirectComposition, which Windows composites above everything a window paints:
  a region cut out of the page's window, a layered window over it, and a
  topmost popup all fail to cover it. So while the field is up, the page is
  laid out below it and comes back when the field closes. The field and its
  suggestions are drawn by the window itself.
- **No caption.** `WM_NCCALCSIZE` takes the title bar away, the band the app
  paints is the drag area, and minimise, maximise and close are drawn at the
  top right as Windows draws them.
- **Text is grayscale on the window's own pass.** ClearType writes no alpha,
  and the window paints into a 32-bit section and hands it over in one go.

## Not here yet

Downloads (the page lists them, nothing starts them), private tabs, the
bookmarks bar, reader mode, split view and extensions.

## Files

```
Windows/
  build.bat          fetch WebView2 SDK + runtime, compile, link
  build\runtime\     the bundled WebView2 runtime, shipped with Search.exe
  make-icon.py       draw src/search.ico from the logomark's path
  src/Address.*      what a typed address means
  src/App.*          the engines, the settings, the app's windows
  src/Com.h          the WRL handler the callbacks are made with
  src/Data.*         history, bookmarks, the session, closed tabs
  src/Input.cpp      the pointer and the keys
  src/Internal.*     the app's own pages
  src/Json.*         a small JSON reader and writer
  src/Main.cpp       the entry point, the theme, the title, the app's actions
  src/Paint.cpp      layout and drawing
  src/Shield.*       the block lists, the slots, the YouTube scripts
  src/Store.*        the folder, the files, the settings
  src/Tab.h          a tab and what it shows
  src/Tabs.cpp       tabs and their web views
  src/Theme.*        colours, metrics, fonts, drawing helpers
  src/WebView.*      the WebView2 environment
  src/Window.*       the window and its messages
  src/Search.manifest, Search.rc
```
