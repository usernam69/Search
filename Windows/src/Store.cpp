#include "Store.h"

#include <shlobj.h>

#include <cwchar>

namespace store {

static std::wstring g_folder;

std::wstring folder() {
    if (!g_folder.empty()) return g_folder;
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) {
        g_folder = std::wstring(buf) + L"\\Search";
    } else {
        g_folder = L"C:\\Search";
    }
    CreateDirectoryW(g_folder.c_str(), nullptr);
    return g_folder;
}

std::wstring file(const std::wstring& name) { return folder() + L"\\" + name; }

void log(const wchar_t* text) {
    std::wstring path = file(L"run.log");
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, nullptr, FILE_END);
    int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string bytes;
    bytes.resize(need);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes.data(), need, nullptr, nullptr);
    DWORD put = 0;
    if (need > 1) WriteFile(h, bytes.data(), (DWORD)bytes.size() - 1, &put, nullptr);
    const char nl[2] = {13, 10};
    WriteFile(h, nl, 2, &put, nullptr);
    CloseHandle(h);
}

std::wstring readText(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::string bytes;
    bytes.resize((size_t)size.QuadPart);
    DWORD got = 0;
    if (!bytes.empty()) ReadFile(h, bytes.data(), (DWORD)bytes.size(), &got, nullptr);
    CloseHandle(h);
    bytes.resize(got);
    if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);
    int need = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    std::wstring out;
    out.resize(need);
    if (need) MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), out.data(), need);
    return out;
}

// Written beside the file and swapped in, so a crash mid-write leaves the old
// one whole rather than half of the new one.
void writeText(const std::wstring& path, const std::wstring& text) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes;
    bytes.resize(need);
    if (need) WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), bytes.data(), need, nullptr, nullptr);

    std::wstring temp = path + L".tmp";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    WriteFile(h, bytes.data(), (DWORD)bytes.size(), &put, nullptr);
    CloseHandle(h);
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

static HKEY key(bool create) {
    HKEY k = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Search", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr);
    return k;
}

std::optional<std::wstring> setting(const wchar_t* name) {
    HKEY k = key(true);
    if (!k) return std::nullopt;
    wchar_t buf[4096];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    std::optional<std::wstring> out;
    if (RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
        if (type == REG_DWORD) {
            out = std::to_wstring(*(DWORD*)buf);
        } else {
            out = std::wstring(buf, size / sizeof(wchar_t));
            while (!out->empty() && out->back() == L'\0') out->pop_back();
        }
    }
    RegCloseKey(k);
    return out;
}

void setSetting(const wchar_t* name, const std::wstring& value) {
    HKEY k = key(true);
    if (!k) return;
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)value.c_str(),
                   (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

void setSetting(const wchar_t* name, const wchar_t* value) { setSetting(name, std::wstring(value)); }

void setSetting(const wchar_t* name, bool value) { setSetting(name, std::wstring(value ? L"1" : L"0")); }

void setSetting(const wchar_t* name, int value) { setSetting(name, std::to_wstring(value)); }

bool settingBool(const wchar_t* name, bool fallback) {
    auto v = setting(name);
    if (!v) return fallback;
    return *v == L"1" || *v == L"true" || *v == L"YES";
}

int settingInt(const wchar_t* name, int fallback) {
    auto v = setting(name);
    if (!v) return fallback;
    return _wtoi(v->c_str());
}

}  // namespace store
