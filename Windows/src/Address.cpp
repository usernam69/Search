#include "Address.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>

namespace address {

std::wstring trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::iswspace(s[a])) ++a;
    while (b > a && std::iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

static bool allDigits(const std::wstring& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// Splits on `sep`, keeping empty pieces, so that
// split(omittingEmptySubsequences: false).
static std::vector<std::wstring> split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    for (;;) {
        size_t at = s.find(sep, start);
        if (at == std::wstring::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, at - start));
        start = at + 1;
    }
}

bool privateRange(const std::wstring& host) {
    auto parts = split(host, L'.');
    if (parts.size() != 4) return false;
    std::vector<int> octets;
    for (auto& p : parts) {
        if (!allDigits(p)) return false;
        long v = std::wcstol(p.c_str(), nullptr, 10);
        if (v < 0 || v > 255) return false;
        octets.push_back((int)v);
    }
    return octets[0] == 10 ||
           (octets[0] == 192 && octets[1] == 168) ||
           (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31);
}

bool looksLikeHost(const std::wstring& host) {
    if (host == L"localhost") return true;

    // Four numbers is an address on the local network as often as not.
    auto numbers = split(host, L'.');
    if (numbers.size() == 4 && std::all_of(numbers.begin(), numbers.end(), [](const std::wstring& n) {
            if (!allDigits(n) || n.size() > 3) return false;
            return std::wcstol(n.c_str(), nullptr, 10) <= 255;
        })) {
        return true;
    }

    auto labels = split(host, L'.');
    if (labels.size() < 2) return false;
    for (auto& label : labels) {
        if (label.empty()) return false;
        if (label.front() == L'-' || label.back() == L'-') return false;
        for (wchar_t c : label) {
            if (!std::iswalnum(c) && c != L'-') return false;
        }
    }

    // The last label carries the weight: a dotted thing ending in letters is
    // a domain, a dotted thing ending in digits is a version number.
    const std::wstring& tld = labels.back();
    return tld.size() >= 2 && std::all_of(tld.begin(), tld.end(), [](wchar_t c) { return std::iswalpha(c); });
}

std::optional<std::wstring> url(const std::wstring& typed) {
    std::wstring text = trim(typed);
    if (text.empty()) return std::nullopt;
    if (text.find(L' ') != std::wstring::npos) return std::nullopt;

    // Written with a scheme, it is taken at its word.
    size_t splitAt = text.find(L"://");
    if (splitAt != std::wstring::npos) {
        std::wstring scheme = lower(text.substr(0, splitAt));
        if (!ours(scheme)) return std::nullopt;
        return text;
    }
    std::wstring lowText = lower(text);
    if (lowText.rfind(L"about:", 0) == 0 || lowText.rfind(L"data:", 0) == 0) return text;

    // Everything else has to look like a host before it gets a scheme put in
    // front of it.
    std::wstring head = text.substr(0, text.find_first_of(L"/?#"));
    if (head.find(L'@') != std::wstring::npos) return std::nullopt;  // an email address
    // This PC by its IPv6 address, [::1]:3000, is a place too.
    if (head.rfind(L"[::1]", 0) == 0) return L"http://" + text;
    std::wstring hostPart = head.substr(0, head.find(L':'));
    if (!looksLikeHost(hostPart)) return std::nullopt;

    // A local server almost never has a certificate, so https there is a
    // connection failure rather than a page. The same goes for a device on
    // the network by its name, printer.local or homeassistant.local.
    bool local = hostPart == L"localhost" ||
                 (hostPart.size() > 10 && hostPart.compare(hostPart.size() - 10, 10, L".localhost") == 0) ||
                 (hostPart.size() > 6 && hostPart.compare(hostPart.size() - 6, 6, L".local") == 0) ||
                 hostPart == L"127.0.0.1" || hostPart == L"0.0.0.0" || privateRange(hostPart);
    return (local ? L"http://" : L"https://") + text;
}

std::optional<std::wstring> reachable(const std::wstring& u) {
    std::wstring h = host(u);
    if (h != L"0.0.0.0") return std::nullopt;
    std::wstring scheme = lower(u.substr(0, u.find(L':')));
    if (scheme != L"http" && scheme != L"https") return std::nullopt;
    size_t at = u.find(L"0.0.0.0");
    std::wstring out = u;
    out.replace(at, 7, L"localhost");
    return out;
}

std::wstring host(const std::wstring& u) {
    size_t at = u.find(L"://");
    if (at == std::wstring::npos) return L"";
    std::wstring rest = u.substr(at + 3);
    if (rest.rfind(L"//", 0) == 0) rest = rest.substr(2);
    rest = rest.substr(0, rest.find_first_of(L"/?#"));
    size_t userAt = rest.rfind(L'@');
    if (userAt != std::wstring::npos) rest = rest.substr(userAt + 1);
    if (!rest.empty() && rest.front() == L'[') {
        size_t close = rest.find(L']');
        if (close != std::wstring::npos) return rest.substr(0, close + 1);
    }
    return lower(rest.substr(0, rest.find(L':')));
}

std::wstring editable(const std::wstring& page) {
    size_t at = page.find(L"://");
    if (at == std::wstring::npos) return page;
    std::wstring scheme = page.substr(0, at);
    std::wstring shortForm = page.substr(at + 3);

    std::vector<std::wstring> candidates{shortForm};
    // A bare "/" is left off only when nothing follows it.
    bool bareRoot = shortForm.size() > 0 && shortForm.back() == L'/' &&
                    shortForm.find(L'?') == std::wstring::npos &&
                    shortForm.find(L'#') == std::wstring::npos &&
                    host(page) + L"/" == shortForm;
    if (bareRoot) candidates.insert(candidates.begin(), shortForm.substr(0, shortForm.size() - 1));

    for (auto& candidate : candidates) {
        auto back = url(candidate);
        if (!back) continue;
        if (*back == page || *back + L"/" == page) return candidate;
    }
    return page;
}

std::wstring withoutWWW(const std::wstring& a) {
    if (a.size() > 4 && lower(a.substr(0, 4)) == L"www.") return a.substr(4);
    return a;
}

std::wstring pretty(const std::wstring& u) {
    std::wstring h = host(u);
    if (h.empty()) return u;
    if (h.size() > 4 && h.rfind(L"www.", 0) == 0) h = h.substr(4);
    size_t at = u.find(L"://");
    std::wstring rest = at == std::wstring::npos ? L"" : u.substr(at + 3);
    size_t cut = rest.find_first_of(L"/?#");
    rest = cut == std::wstring::npos ? L"" : rest.substr(cut);
    if (rest.empty() || rest == L"/") return h;
    return h + rest;
}

std::optional<std::wstring> fileFrom(const std::wstring& typed) {
    std::wstring text = trim(typed);
    if (text.empty()) return std::nullopt;
    // A drive letter, a UNC path, or anything with a separator in it: a path
    // rather than a host. A word with a dot in it is a host.
    bool pathish = (text.size() >= 3 && std::iswalpha(text[0]) && text[1] == L':' &&
                    (text[2] == L'\\' || text[2] == L'/')) ||
                   text.rfind(L"\\\\", 0) == 0 || text.rfind(L"//", 0) == 0;
    if (!pathish) return std::nullopt;
    DWORD attrs = GetFileAttributesW(text.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return std::nullopt;
    return text;
}

std::wstring fileURL(const std::wstring& path) {
    wchar_t buf[32768];
    DWORD len = (DWORD)path.size();
    if (len == 0 || len >= 32768) return L"";
    wcscpy_s(buf, path.c_str());
    for (DWORD i = 0; i < len; ++i) {
        if (buf[i] == L'\\') buf[i] = L'/';
    }
    std::wstring out = L"file:///";
    // Percent-encode what a URL can't carry: spaces, #, ? and non-ASCII.
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::string utf8;
    int need = WideCharToMultiByte(CP_UTF8, 0, buf, (int)len, nullptr, 0, nullptr, nullptr);
    if (need > 0) {
        utf8.resize(need);
        WideCharToMultiByte(CP_UTF8, 0, buf, (int)len, utf8.data(), need, nullptr, nullptr);
    }
    for (unsigned char c : utf8) {
        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (wchar_t)c;
        } else {
            out += L'%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

}  // namespace address
