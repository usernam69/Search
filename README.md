# Search

A small, fast, quiet web browser for Windows, by [Office Commun](https://officecommun.com).

## What it is

Search is a browser with nothing in the way. A column of tabs down the left and
the page as a card beside it. There is no toolbar, no start page, no sidebar of
suggestions, no account to sign into, nothing that wants your attention. You
type an address or a few words in one field and you are on the page.

It uses **WebView2**, the Chromium engine that Microsoft Edge also uses, but
carries its own fixed copy of that runtime. Nothing has to be installed on the
PC first: the app and its runtime ship together.

## What it does

- **One field.** Type an address and you go there; type words and you search.
  It finishes addresses from your own history, bookmarks and open tabs, and
  never sends what you type anywhere until you press Return.
- **Tabs that stay out of the way.** A column of rows, each the page's own
  title. Pinned pages shrink to a letter at the top of the column. Tabs from
  your last session come back instantly and cost nothing until you pick them.
- **An ad blocker that runs before the page.** Third-party trackers and ad
  networks are stopped at the network level, so there is nothing to render and
  nothing to slow down. On by default, off per site if something breaks.
- **The app's own pages.** Settings, History, Bookmarks and Downloads are pages
  drawn in the app's own colours rather than windows of their own.
- **Your data stays on this PC.** History, bookmarks, the session and the sites
  Shield is off for are small JSON files in `%LOCALAPPDATA%\Search`; settings
  live in the registry under `HKCU\Software\Search`. Nothing is synced.

## Building

Needs Visual Studio 2022 Build Tools with the C++ workload and the Windows SDK.
Then:

```bat
cd Windows
build.bat
```

It fetches the WebView2 SDK and runtime on first use and writes
`build\Search.exe` plus a `build\runtime` folder. Ship both. See
[Windows/README.md](Windows/README.md) for what it does, the shortcuts, and how
the sources are laid out.

## Licence

See [LICENSE](LICENSE).
