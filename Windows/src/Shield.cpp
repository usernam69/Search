#include "Shield.h"

#include <algorithm>
#include <set>

#include "Address.h"
#include "Store.h"

namespace shield {

// Third parties whose only job is to watch or to sell. First-party requests
// are untouched: a site's own scripts are the site.
static const wchar_t* kUnwanted[] = {
    L"doubleclick.net", L"googlesyndication.com", L"googleadservices.com",
    L"googletagservices.com", L"google-analytics.com", L"googletagmanager.com",
    L"adservice.google.com", L"amazon-adsystem.com", L"adnxs.com", L"adsrvr.org",
    L"criteo.com", L"criteo.net", L"taboola.com", L"outbrain.com",
    L"rubiconproject.com", L"pubmatic.com", L"openx.net", L"casalemedia.com",
    L"smartadserver.com", L"sharethrough.com", L"indexww.com", L"bidswitch.net",
    L"33across.com", L"teads.tv", L"moatads.com", L"adroll.com",
    L"scorecardresearch.com", L"quantserve.com", L"chartbeat.com",
    L"hotjar.com", L"mouseflow.com", L"fullstory.com", L"clarity.ms",
    L"mixpanel.com", L"amplitude.com", L"segment.com", L"segment.io",
    L"branch.io", L"appsflyer.com", L"adjust.com", L"analytics.tiktok.com",
    L"connect.facebook.net", L"ads-twitter.com", L"analytics.twitter.com",
};

// The few slots that are reliably an advertisement and nothing else. Kept
// deliberately short — a generous cosmetic list is how a blocker starts eating
// the page it was meant to clean.
static const wchar_t* kSlots =
    L".adsbygoogle, ins.adsbygoogle, [id^=\"google_ads_\"], [id^=\"div-gpt-ad\"], "
    L"[id^=\"taboola-\"], #taboola-below-article, iframe[src*=\"doubleclick.net\"], "
    L"iframe[src*=\"googlesyndication\"], iframe[src*=\"amazon-adsystem\"], "
    L".ad-slot, .ad-slot-container, .top-banner-ad-container, .ad-leaderboard, "
    L".ad-billboard, .ad-giga, .ad-mpu, .ad-mrec, .ad-unit, .adunit, .adslot, "
    L".dfp-ad, .gpt-ad, .w_ad";

struct SiteSlots {
    const wchar_t* site;
    const wchar_t* selector;
};

// Slots with names too plain, or too much a site's own, to hide everywhere —
// AS and El País call theirs just `.ad` — hidden only on the sites EasyList's
// own site rules hide them on.
static const SiteSlots kSlotsBySite[] = {
    {L"as.com", L".ad"},
    {L"elpais.com", L".ad"},
    {L"theguardian.com", L".top-fronts-banner-ad-container"},
    {L"independent.co.uk", L"#billboard-wrapper"},
    {L"the-independent.com", L"#billboard-wrapper"},
    {L"cnn.com", L".ad-slot-header__wrapper"},
};

static bool g_enabled = true;
static std::set<std::wstring> g_paused;

static void loadPaused() {
    auto text = store::readText(store::file(L"shield-paused.json"));
    // One host a line: the smallest file that is also readable by hand.
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring host = address::trim(text.substr(at, end - at));
        if (!host.empty()) g_paused.insert(address::lower(host));
        at = end + 1;
    }
}

static void savePaused() {
    std::wstring text;
    for (auto& host : g_paused) {
        text += host;
        text += L'\n';
    }
    store::writeText(store::file(L"shield-paused.json"), text);
}

static bool loaded = false;

bool enabled() {
    if (!loaded) {
        g_enabled = store::settingBool(L"shield.on", true);
        loadPaused();
        loaded = true;
    }
    return g_enabled;
}

void setEnabled(bool on) {
    enabled();
    g_enabled = on;
    store::setSetting(L"shield.on", on);
}

bool pausedOn(const std::wstring& host) {
    enabled();
    if (host.empty()) return false;
    return g_paused.count(address::lower(host)) > 0;
}

void pause(const std::wstring& host, bool off) {
    enabled();
    if (host.empty()) return;
    std::wstring h = address::lower(host);
    if (off) {
        g_paused.insert(h);
    } else {
        g_paused.erase(h);
    }
    savePaused();
}

// Whether `host` is `domain` or a subdomain of it. A bare "ads.example.com"
// in the list also stops "cdn.ads.example.com": an ad network's own CDN is
// the ad network.
static bool under(const std::wstring& host, const std::wstring& domain) {
    if (host == domain) return true;
    if (host.size() <= domain.size()) return false;
    size_t at = host.size() - domain.size();
    return host.compare(at, domain.size(), domain) == 0 && host[at - 1] == L'.';
}

bool shouldBlock(const std::wstring& url, const std::wstring& pageHost) {
    if (!enabled() || pausedOn(pageHost)) return false;
    std::wstring scheme = address::lower(url.substr(0, url.find(L':')));
    if (scheme != L"http" && scheme != L"https") return false;
    std::wstring host = address::host(url);
    if (host.empty()) return false;
    // First party: the page's own host, or anything it shares a name with.
    if (under(host, pageHost) || under(pageHost, host)) return false;

    for (auto* domain : kUnwanted) {
        if (under(host, domain)) return true;
    }
    return false;
}

bool isYouTube(const std::wstring& host) {
    static const wchar_t* hosts[] = {
        L"youtube.com", L"www.youtube.com", L"m.youtube.com", L"music.youtube.com",
        L"youtube-nocookie.com", L"www.youtube-nocookie.com",
    };
    for (auto* h : hosts) {
        if (host == h) return true;
    }
    return false;
}

std::wstring styleFor(const std::wstring& pageHost) {
    if (!enabled() || pausedOn(pageHost)) return L"";
    std::wstring css = std::wstring(kSlots) + L" { display: none !important; }\n";
    for (auto& entry : kSlotsBySite) {
        if (under(pageHost, entry.site)) {
            css += std::wstring(entry.selector) + L" { display: none !important; }\n";
        }
    }
    return css;
}

// In the page's own world, before any of its scripts. YouTube announces its
// ads in the player's own data, so the ad schedule is taken out of that data
// before the player reads it. Nothing else is touched; an answer of any other
// shape is handed on as it came.
static const wchar_t* kYouTubePage = LR"JS(
(function () {
  'use strict';
  var hosts = ['youtube.com','www.youtube.com','m.youtube.com','music.youtube.com','youtube-nocookie.com','www.youtube-nocookie.com'];
  if (hosts.indexOf(location.hostname) < 0) return;
  var apply = Reflect.apply, owns = Object.prototype.hasOwnProperty,
      define = Object.defineProperty, isArray = Array.isArray;
  var ads = ['adPlacements', 'adSlots', 'playerAds', 'adBreakHeartbeatParams'];
  var marks = ['responseContext', 'playabilityStatus', 'streamingData', 'videoDetails'];
  function plain(o) { return o !== null && typeof o === 'object' && !isArray(o); }
  function youtubes(o) {
    for (var i = 0; i < marks.length; i++) if (apply(owns, o, [marks[i]])) return true;
    return false;
  }
  function strip(o) {
    if (!plain(o) || !youtubes(o)) return;
    for (var i = 0; i < ads.length; i++) if (apply(owns, o, [ads[i]])) delete o[ads[i]];
  }
  function one(o) {
    if (!plain(o)) return;
    strip(o);
    if (apply(owns, o, ['playerResponse'])) strip(o.playerResponse);
  }
  function prune(value) {
    try {
      if (isArray(value)) {
        for (var i = 0; i < value.length && i < 8; i++) one(value[i]);
      } else {
        one(value);
      }
    } catch (e) {}
    return value;
  }
  try {
    var parse = JSON.parse;
    if (typeof parse === 'function') {
      define(JSON, 'parse', {
        value: ({ parse(text, reviver) { return prune(apply(parse, JSON, arguments)); } }).parse,
        writable: true, enumerable: false, configurable: true
      });
    }
  } catch (e) {}
  try {
    var json = typeof Response === 'function' && Response.prototype.json;
    var then = Promise.prototype.then;
    if (typeof json === 'function') {
      define(Response.prototype, 'json', {
        value: ({ json() { return apply(then, apply(json, this, []), [prune]); } }).json,
        writable: true, enumerable: true, configurable: true
      });
    }
  } catch (e) {}
  ['ytInitialPlayerResponse', 'ytInitialData'].forEach(function (name) {
    try {
      if (apply(owns, window, [name])) return;
      var held;
      define(window, name, {
        get: function () { return held; },
        set: function (value) { held = prune(value); },
        enumerable: true, configurable: true
      });
    } catch (e) {}
  });
})();
)JS";

// An ad that plays all the same goes by muted and fast, and the video comes
// back as it was. It never seeks: an ad stitched into the video itself would
// make a jump to "the end" the end of the video.
static const wchar_t* kYouTubeSkipper = LR"JS(
(function () {
  'use strict';
  var hosts = ['youtube.com','www.youtube.com','m.youtube.com','music.youtube.com','youtube-nocookie.com','www.youtube-nocookie.com'];
  if (hosts.indexOf(location.hostname) < 0) return;
  var held = new WeakMap();
  function look(event) {
    var video = event.target;
    if (!(video instanceof HTMLVideoElement)) return;
    var player = video.closest('.html5-video-player');
    var ad = !!player && player.classList.contains('ad-showing');
    var was = held.get(video);
    try {
      if (ad) {
        if (!was) {
          was = { muted: video.muted, rate: video.playbackRate };
          held.set(video, was);
          video.muted = true;
        }
        if (video.playbackRate !== 16) video.playbackRate = 16;
      } else if (was) {
        held.delete(video);
        video.playbackRate = was.rate;
        video.muted = was.muted;
      }
    } catch (e) {}
  }
  ['playing', 'timeupdate', 'durationchange', 'emptied'].forEach(function (type) {
    document.addEventListener(type, look, true);
  });
})();
)JS";

std::wstring youtubeScript(const std::wstring& pageHost) {
    if (!enabled() || pausedOn(pageHost) || !isYouTube(pageHost)) return L"";
    return std::wstring(kYouTubePage) + L"\n" + kYouTubeSkipper;
}

}  // namespace shield
