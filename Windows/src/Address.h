// What you type has to be a place. There is no search here, so this either
// hands back a URL or hands back nothing — and nothing is worth saying out
// loud, because the alternative is a browser that silently does something else
// with your keystrokes.
//
// The rules are Address.swift's, on the Mac, rule for rule: what counts as a
// host, when https is put in front and when http is (this machine, the local
// network), what an email address is not, and how an address is shown back to
// you for editing.
#pragma once

#include <string>
#include <optional>
#include <vector>

namespace address {

// Schemes the window can show itself. Anything else typed with a scheme —
// mailto:, a custom app link — is somebody else's job and gets refused here
// rather than opening a blank tab.
inline bool ours(const std::wstring& scheme) {
    return scheme == L"http" || scheme == L"https" || scheme == L"file" ||
           scheme == L"about" || scheme == L"data" || scheme == L"search";
}

std::wstring trim(const std::wstring& s);
std::wstring lower(std::wstring s);

// The private ranges, where Docker and many offices put their machines.
// Four numbers and nothing more: 10.io is a website, not 10.0.0.0/8.
bool privateRange(const std::wstring& host);

// A host is a name with a dot in it ending in letters, four numbers, or
// localhost. "hello world" is not a website, and neither is "todo".
bool looksLikeHost(const std::wstring& host);

// The place `typed` means, or nothing. Only what is typed and committed comes
// here; suggestions and dropped text never do.
std::optional<std::wstring> url(const std::wstring& typed);

// Dev servers say they are listening on 0.0.0.0 — every address this PC has —
// and print that as the address to open. It is opened as localhost, the same
// server.
std::optional<std::wstring> reachable(const std::wstring& url);

// A page's address as the field shows it for editing: something that, sent as
// it is, goes back to the same page.
std::wstring editable(const std::wstring& page);

// An address as a row shows it: without a leading www.
std::wstring withoutWWW(const std::wstring& address);

// What a tab says before the page has told us its title: the address, with
// the parts nobody reads taken off.
std::wstring pretty(const std::wstring& url);

// The host of an address, for the site card and Shield's per-site switch.
std::wstring host(const std::wstring& url);

// A Windows path typed into the field, as /Users/… and ~/… are on the Mac.
std::optional<std::wstring> fileFrom(const std::wstring& typed);

// A file: address for a path on this PC.
std::wstring fileURL(const std::wstring& path);

}  // namespace address
