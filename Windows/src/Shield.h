// The ad blocker. No settings, no counter, no shield icon going green — it is
// simply true that the page is lighter.
//
// It is enforced at the network level, before anything is rendered: CEF's
// resource-request hook, which cancels a request before it leaves the browser,
// plus a stylesheet put into each page at document start so the empty boxes an
// ad leaves behind never draw.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace shield {

bool enabled();
void setEnabled(bool on);

// Sites it is off for — the ones it broke.
bool pausedOn(const std::wstring& host);
void pause(const std::wstring& host, bool off);

// Whether a request to `url` from a page on `pageHost` is one to stop.
// Third-party ad and tracker hosts only: a site's own scripts are the site.
bool shouldBlock(const std::wstring& url, const std::wstring& pageHost);

// The stylesheet that hides the slots, for a page on `pageHost`.
std::wstring styleFor(const std::wstring& pageHost);

// YouTube's own ads, taken out of the player's data before it reads it. Only
// on YouTube's hosts, and only while Shield is on and not paused there.
std::wstring youtubeScript(const std::wstring& pageHost);
bool isYouTube(const std::wstring& host);

}  // namespace shield
