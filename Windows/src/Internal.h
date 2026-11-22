// The app's own pages: New Tab, Settings, History, Bookmarks, Downloads.
//
// They are real pages in a tab: something you can leave open,
// come back to with Ctrl+Tab and close with Ctrl+W like any other. Written as
// HTML here so they scroll and can be searched with Ctrl+F, in the app's own
// colours.
#pragma once

#include <string>

// "newtab", "settings", "history", "bookmarks", "downloads".
std::wstring internalTitle(const std::wstring& page);
std::wstring internalURL(const std::wstring& page);
std::wstring internalPath(const std::wstring& url);

// The page itself.
std::wstring internalHTML(const std::wstring& page);

// "search://open?u=…", "search://settings/shield", "search://history/clear":
// an instruction to the app rather than a page to show.
bool isInternalAction(const std::wstring& url);
