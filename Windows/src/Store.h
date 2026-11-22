// What every window shares: the settings, History, bookmarks and the sites
// Shield is off for. Kept in small JSON files next to the app's own, as on the
// Mac, so a copy of Search on either machine reads the same shapes.
#pragma once

#include <windows.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace store {

// %LOCALAPPDATA%\Search, made on first use.
std::wstring folder();
std::wstring file(const std::wstring& name);

// A line in run.log beside the app's files: what started, what failed.
void log(const wchar_t* text);

std::wstring readText(const std::wstring& path);
void writeText(const std::wstring& path, const std::wstring& text);

// Settings live under HKCU\Software\Search, which is what Windows has instead
// of the Mac's defaults, and is what the Settings page writes to.
std::optional<std::wstring> setting(const wchar_t* key);
void setSetting(const wchar_t* key, const std::wstring& value);
// A string literal would otherwise choose the bool overload, which a pointer
// converts to more readily than to a std::wstring.
void setSetting(const wchar_t* key, const wchar_t* value);
void setSetting(const wchar_t* key, bool value);
void setSetting(const wchar_t* key, int value);
bool settingBool(const wchar_t* key, bool fallback);
int settingInt(const wchar_t* key, int fallback);

}  // namespace store
