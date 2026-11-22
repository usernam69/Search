#include "Internal.h"

#include <algorithm>
#include <functional>

#include "Address.h"
#include "App.h"
#include "Data.h"
#include "Shield.h"
#include "Store.h"
#include "Theme.h"

static const wchar_t* kScheme = L"search://";

static std::wstring escape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::wstring internalPath(const std::wstring& url) {
    std::wstring text = url;
    if (text.rfind(kScheme, 0) == 0) text = text.substr(wcslen(kScheme));
    if (text.rfind(L"search.internal/", 0) == 0) text = text.substr(16);
    // The page is named after the scheme's slashes, if it came with them.
    while (!text.empty() && text.front() == L'/') text.erase(text.begin());
    size_t cut = text.find_first_of(L"?#");
    if (cut != std::wstring::npos) text = text.substr(0, cut);
    while (!text.empty() && text.back() == L'/') text.pop_back();
    if (text.empty()) text = L"newtab";
    return text;
}

std::wstring internalTitle(const std::wstring& page) {
    if (page == L"settings") return L"Settings";
    if (page == L"history") return L"History";
    if (page == L"bookmarks") return L"Bookmarks";
    if (page == L"downloads") return L"Downloads";
    return L"New Tab";
}

std::wstring internalURL(const std::wstring& page) { return std::wstring(kScheme) + page; }

bool isInternalAction(const std::wstring& url) {
    if (url.rfind(L"search.internal/", 0) == 0) {
        std::wstring rest = url.substr(16);
        return rest.rfind(L"open?", 0) == 0 || rest.rfind(L"act/", 0) == 0;
    }
    return false;
}

// ---- the pages ----

static std::wstring shell(const std::wstring& title, const std::wstring& body, const std::wstring& script = L"") {
    const Palette& p = g_theme.palette;
    auto hex = [](Colour c) {
        wchar_t buf[16];
        swprintf(buf, 16, L"#%02x%02x%02x", c.r, c.g, c.b);
        return std::wstring(buf);
    };
    std::wstring html = LR"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>)HTML";
    html += escape(title);
    html += LR"HTML(</title><style>
:root { color-scheme: )HTML";
    html += g_theme.dark ? L"dark" : L"light";
    html += LR"HTML(; }
* { box-sizing: border-box; }
body {
  margin: 0; padding: 40px 48px 64px;
  background: )HTML" + hex(p.card) + LR"HTML(;
  color: )HTML" + hex(p.ink) + LR"HTML(;
  font: 13.5px/1.55 "Segoe UI Variable Text", "Segoe UI", system-ui, sans-serif;
  -webkit-font-smoothing: antialiased;
}
h1 { font-size: 22px; font-weight: 600; margin: 0 0 4px; letter-spacing: -0.01em; }
h2 { font-size: 13px; font-weight: 600; margin: 34px 0 8px; color: )HTML" + hex(p.muted) + LR"HTML(;
     text-transform: uppercase; letter-spacing: 0.06em; }
p.lede { margin: 0 0 28px; color: )HTML" + hex(p.muted) + LR"HTML(; }
a { color: inherit; text-decoration: none; }
.row {
  display: flex; align-items: center; gap: 12px;
  padding: 9px 12px; margin: 0 -12px; border-radius: 8px; cursor: default;
}
.row:hover { background: )HTML" + hex(p.wash) + LR"HTML(; }
.row .t { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.row .u { color: )HTML" + hex(p.muted) + LR"HTML(; font-size: 12px; flex: 1; min-width: 0;
           overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.row .when { color: )HTML" + hex(p.muted) + LR"HTML(; font-size: 12px; }
.empty { color: )HTML" + hex(p.muted) + LR"HTML(; padding: 8px 0; }
.section { border-top: 1px solid )HTML" + hex(p.hairline) + LR"HTML(; margin-top: 22px; padding-top: 4px; }
.switch { display: flex; align-items: center; justify-content: space-between; padding: 10px 12px;
          margin: 0 -12px; border-radius: 8px; cursor: default; }
.switch:hover { background: )HTML" + hex(p.wash) + LR"HTML(; }
.switch .label { font-size: 13.5px; }
.switch .hint { color: )HTML" + hex(p.muted) + LR"HTML(; font-size: 12px; margin-top: 1px; }
.toggle { width: 38px; height: 22px; border-radius: 11px; background: )HTML" + hex(p.faint) + LR"HTML(;
          position: relative; transition: background .15s; flex: none; }
.toggle.on { background: )HTML" + hex(p.safe) + LR"HTML(; }
.toggle::after { content: ""; position: absolute; top: 2px; left: 2px; width: 18px; height: 18px;
                 border-radius: 9px; background: #fff; transition: transform .15s; }
.toggle.on::after { transform: translateX(16px); }
select {
  font: inherit; padding: 5px 8px; border-radius: 7px; border: 1px solid )HTML" + hex(p.fieldStroke) + LR"HTML(;
  background: )HTML" + hex(p.field) + LR"HTML(; color: inherit;
}
.key { color: )HTML" + hex(p.muted) + LR"HTML(; font-size: 12px; }
button {
  font: inherit; padding: 6px 12px; border-radius: 7px; border: 1px solid )HTML" + hex(p.fieldStroke) + LR"HTML(;
  background: )HTML" + hex(p.field) + LR"HTML(; color: inherit; cursor: default;
}
button:hover { background: )HTML" + hex(p.hover) + LR"HTML(; }
</style></head><body>
)HTML";
    html += body;
    if (!script.empty()) {
        html += L"<script>";
        html += script;
        html += L"</script>";
    }
    html += L"</body></html>";
    return html;
}

static std::wstring newTabPage() {
    // There is no start page: a new tab is empty, and the field is where you
    // type. This one is the same, with the shortcuts listed.
    std::wstring body;
    body += L"<h1>Search</h1>";
    body += L"<p class=\"lede\">Type an address or a few words above.</p>";
    body += L"<div class=\"section\"></div><h2>Keyboard</h2>";
    struct Key { const wchar_t* keys; const wchar_t* what; };
    static const Key keys[] = {
        {L"Ctrl+L", L"the address field"},
        {L"Ctrl+T", L"a new tab"},
        {L"Ctrl+W", L"close the tab"},
        {L"Ctrl+Shift+T", L"the tab you just closed"},
        {L"Ctrl+Tab", L"the next tab"},
        {L"Ctrl+1–9", L"a tab by its place"},
        {L"Ctrl+R", L"reload the page"},
        {L"Alt+←", L"back"},
        {L"Ctrl+F", L"find on the page"},
        {L"Ctrl+D", L"bookmark this page"},
        {L"Ctrl+H", L"history"},
        {L"Ctrl+,", L"settings"},
    };
    for (auto& k : keys) {
        body += L"<div class=\"row\"><span class=\"t\">";
        body += k.what;
        body += L"</span><span class=\"key\">";
        body += k.keys;
        body += L"</span></div>";
    }
    return shell(L"New Tab", body);
}

static std::wstring historyPage() {
    std::wstring body = L"<h1>History</h1>";
    auto visits = history::read();
    if (visits.empty()) {
        body += L"<p class=\"empty\">Nothing yet.</p>";
        return shell(L"History", body);
    }
    body += L"<p class=\"lede\">";
    body += std::to_wstring(visits.size());
    body += L" pages.</p>";
    for (auto& v : visits) {
        body += L"<a class=\"row\" href=\"";
        body += escape(v.url);
        body += L"\"><span class=\"t\">";
        body += escape(v.title.empty() ? address::pretty(v.url) : v.title);
        body += L"</span><span class=\"u\">";
        body += escape(address::pretty(v.url));
        body += L"</span></a>";
    }
    return shell(L"History", body);
}

static std::wstring bookmarksPage() {
    std::wstring body = L"<h1>Bookmarks</h1>";
    auto list = bookmarks::read();
    if (list.empty()) {
        body += L"<p class=\"empty\">Nothing kept yet. Ctrl+D keeps the page you are on.</p>";
        return shell(L"Bookmarks", body);
    }
    std::function<void(const std::vector<Bookmark>&, int)> walk =
        [&](const std::vector<Bookmark>& items, int depth) {
            for (auto& b : items) {
                if (b.isFolder()) {
                    body += L"<h2 style=\"margin-left:";
                    body += std::to_wstring(depth * 16);
                    body += L"px\">";
                    body += escape(b.title);
                    body += L"</h2>";
                    walk(b.children, depth + 1);
                } else {
                    body += L"<a class=\"row\" href=\"";
                    body += escape(b.url);
                    body += L"\"><span class=\"t\">";
                    body += escape(b.title.empty() ? address::pretty(b.url) : b.title);
                    body += L"</span><span class=\"u\">";
                    body += escape(address::pretty(b.url));
                    body += L"</span></a>";
                }
            }
        };
    walk(list, 0);
    return shell(L"Bookmarks", body);
}

static std::wstring settingsPage() {
    std::wstring body = L"<h1>Settings</h1>";
    body += L"<p class=\"lede\">What is simply better is on for everyone. These are the choices.</p>";

    body += L"<div class=\"section\"></div><h2>Privacy</h2>";
    body += L"<div class=\"switch\" data-act=\"shield\"><div><div class=\"label\">Block ads and trackers</div>";
    body += L"<div class=\"hint\">Third-party ad and tracker requests are stopped before they leave, ";
    body += L"and the boxes they leave behind never draw.</div></div><div class=\"toggle";
    body += shield::enabled() ? L" on" : L"";
    body += L"\"></div></div>";

    body += L"<div class=\"section\"></div><h2>Search</h2>";
    body += L"<div class=\"switch\"><div><div class=\"label\">Search engine</div>";
    body += L"<div class=\"hint\">What words typed in the field ask.</div></div>";
    body += L"<select id=\"engine\">";
    for (auto& e : engines()) {
        body += L"<option value=\"";
        body += escape(e.id);
        body += L"\"";
        if (e.id == app::engineID()) body += L" selected";
        body += L">";
        body += escape(e.title);
        body += L"</option>";
    }
    body += L"</select></div>";
    body += L"<div class=\"switch\"><div><div class=\"label\">Custom engine</div>";
    body += L"<div class=\"hint\">A search address with %s where the words go.</div></div>";
    body += L"<input id=\"custom\" style=\"font:inherit;padding:5px 8px;border-radius:7px;border:1px solid ";
    {
        wchar_t buf[16];
        swprintf(buf, 16, L"#%02x%02x%02x", g_theme.palette.fieldStroke.r, g_theme.palette.fieldStroke.g,
                 g_theme.palette.fieldStroke.b);
        body += buf;
    }
    body += L";background:transparent;color:inherit;width:280px\" value=\"";
    body += escape(app::customEngineTemplate());
    body += L"\"></div>";

    body += L"<div class=\"section\"></div><h2>Tabs</h2>";
    body += L"<div class=\"switch\" data-act=\"roomy\"><div><div class=\"label\">Roomier tabs</div>";
    body += L"<div class=\"hint\">A taller row in the column, easier to hit.</div></div><div class=\"toggle";
    body += app::roomyTabs() ? L" on" : L"";
    body += L"\"></div></div>";

    body += L"<div class=\"section\"></div><h2>Appearance</h2>";
    body += L"<div class=\"switch\"><div><div class=\"label\">Light, dark, or Windows' own</div></div>";
    body += L"<select id=\"look\">";
    for (auto& look : {std::pair<const wchar_t*, const wchar_t*>{L"system", L"System"},
                       {L"light", L"Light"}, {L"dark", L"Dark"}}) {
        body += L"<option value=\"";
        body += look.first;
        body += L"\"";
        if (app::appearance() == look.first) body += L" selected";
        body += L">";
        body += look.second;
        body += L"</option>";
    }
    body += L"</select></div>";

    body += L"<div class=\"section\"></div><h2>General</h2>";
    body += L"<div class=\"switch\" data-act=\"confirm\"><div><div class=\"label\">Ask before quitting</div>";
    body += L"<div class=\"hint\">Ctrl+Q asks before it closes the window and its tabs.</div></div>";
    body += L"<div class=\"toggle";
    body += app::confirmQuit() ? L" on" : L"";
    body += L"\"></div></div>";

    body += LR"JS(<script>
function tell(action) {
  // A message to the app, not a link: search:// is not a scheme Windows can
  // navigate to, and a failed navigation would leave the page where it was.
  if (window.chrome && window.chrome.webview) window.chrome.webview.postMessage(action);
}
document.querySelectorAll("[data-act]").forEach(function (el) {
  el.addEventListener("click", function () { tell(el.dataset.act); });
});
var engine = document.getElementById("engine");
if (engine) engine.addEventListener("change", function () { tell("engine:" + engine.value); });
var custom = document.getElementById("custom");
if (custom) custom.addEventListener("change", function () { tell("custom:" + encodeURIComponent(custom.value)); });
var look = document.getElementById("look");
if (look) look.addEventListener("change", function () { tell("look:" + look.value); });
</script>)JS";
    return shell(L"Settings", body);
}

std::wstring internalHTML(const std::wstring& page) {
    if (page == L"settings") return settingsPage();
    if (page == L"history") return historyPage();
    if (page == L"bookmarks") return bookmarksPage();
    if (page == L"downloads") return shell(L"Downloads", L"<h1>Downloads</h1><p class=\"empty\">Nothing yet.</p>");
    return newTabPage();
}
