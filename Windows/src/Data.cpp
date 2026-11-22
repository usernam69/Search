#include "Data.h"

#include <windows.h>

#include <algorithm>

#include "Json.h"
#include "Store.h"

// ---- history ----

namespace history {

static std::vector<Visit> g_visits;
static bool g_loaded = false;

static long long now() { return (long long)time(nullptr); }

static std::vector<Visit> load() {
    if (g_loaded) return g_visits;
    g_loaded = true;
    auto parsed = json::parse(store::readText(store::file(L"history.json")));
    if (!parsed) return g_visits;
    const json::Value& root = *parsed;
    const json::Value& list = root.isArray() ? root : root[L"visits"];
    for (size_t i = 0; i < list.size(); ++i) {
        const json::Value& row = list.at(i);
        Visit v;
        v.url = row[L"url"].asString();
        v.title = row[L"title"].asString();
        v.at = (long long)row[L"at"].asNumber();
        v.visits = row[L"visits"].asInt(1);
        if (!v.url.empty()) g_visits.push_back(v);
    }
    return g_visits;
}

std::vector<Visit> read() { return load(); }

void write(const std::vector<Visit>& visits) {
    json::Value list = json::Value::array();
    for (auto& v : visits) {
        json::Value row = json::Value::object();
        row.set(L"url", v.url);
        row.set(L"title", v.title);
        row.set(L"at", (double)v.at);
        row.set(L"visits", v.visits);
        list.push(std::move(row));
    }
    store::writeText(store::file(L"history.json"), list.dump());
}

void note(const std::wstring& url, const std::wstring& title) {
    load();
    auto it = std::find_if(g_visits.begin(), g_visits.end(), [&](const Visit& v) { return v.url == url; });
    if (it != g_visits.end()) {
        it->at = now();
        if (!title.empty()) it->title = title;
        it->visits++;
    } else {
        g_visits.push_back(Visit{url, title, now(), 1});
    }
    flush();
}

void flush() {
    load();
    // The newest first, and a ceiling: history is for finishing addresses, not
    // for keeping every page you ever opened.
    std::sort(g_visits.begin(), g_visits.end(), [](const Visit& a, const Visit& b) { return a.at > b.at; });
    if (g_visits.size() > 4000) g_visits.resize(4000);
    write(g_visits);
}

void clear() {
    g_visits.clear();
    g_loaded = true;
    write(g_visits);
}

}  // namespace history

// ---- bookmarks ----

namespace bookmarks {

static Bookmark fromJson(const json::Value& v) {
    Bookmark b;
    b.id = v[L"id"].asString();
    b.title = v[L"title"].asString();
    b.url = v[L"url"].asString();
    const json::Value& kids = v[L"children"];
    for (size_t i = 0; i < kids.size(); ++i) b.children.push_back(fromJson(kids.at(i)));
    return b;
}

static json::Value toJson(const Bookmark& b) {
    json::Value v = json::Value::object();
    if (!b.id.empty()) v.set(L"id", b.id);
    v.set(L"title", b.title);
    if (!b.url.empty()) v.set(L"url", b.url);
    if (!b.children.empty()) {
        json::Value kids = json::Value::array();
        for (auto& kid : b.children) kids.push(toJson(kid));
        v.set(L"children", std::move(kids));
    }
    return v;
}

std::vector<Bookmark> read() {
    std::vector<Bookmark> out;
    auto parsed = json::parse(store::readText(store::file(L"bookmarks.json")));
    if (!parsed) return out;
    const json::Value& root = *parsed;
    const json::Value& list = root.isArray() ? root : root[L"bookmarks"];
    for (size_t i = 0; i < list.size(); ++i) out.push_back(fromJson(list.at(i)));
    return out;
}

void write(const std::vector<Bookmark>& list) {
    json::Value arr = json::Value::array();
    for (auto& b : list) arr.push(toJson(b));
    store::writeText(store::file(L"bookmarks.json"), arr.dump());
}

}  // namespace bookmarks

// ---- session ----

namespace session {

Session read() {
    Session s;
    auto parsed = json::parse(store::readText(store::file(L"session.json")));
    if (!parsed) return s;
    const json::Value& root = *parsed;
    const json::Value& tabs = root[L"tabs"];
    for (size_t i = 0; i < tabs.size(); ++i) {
        const json::Value& row = tabs.at(i);
        SessionTab t;
        t.url = row[L"url"].asString();
        t.title = row[L"title"].asString();
        t.pin = row[L"pin"].asString();
        t.name = row[L"name"].asString();
        t.home = row[L"home"].asString();
        t.muted = row[L"muted"].asBool();
        if (!t.url.empty()) s.tabs.push_back(t);
    }
    s.active = root[L"active"].asInt(0);
    if (s.active < 0 || (size_t)s.active >= s.tabs.size()) s.active = 0;
    return s;
}

void write(const Session& shape) {
    json::Value root = json::Value::object();
    json::Value tabs = json::Value::array();
    for (auto& t : shape.tabs) {
        json::Value row = json::Value::object();
        row.set(L"url", t.url);
        row.set(L"title", t.title);
        if (!t.pin.empty()) row.set(L"pin", t.pin);
        if (!t.name.empty()) row.set(L"name", t.name);
        if (!t.home.empty()) row.set(L"home", t.home);
        if (t.muted) row.set(L"muted", true);
        tabs.push(std::move(row));
    }
    root.set(L"tabs", std::move(tabs));
    root.set(L"active", shape.active);
    store::writeText(store::file(L"session.json"), root.dump());
}

std::vector<ClosedTab> readClosed() {
    std::vector<ClosedTab> out;
    auto parsed = json::parse(store::readText(store::file(L"closed.json")));
    if (!parsed) return out;
    const json::Value& list = *parsed;
    for (size_t i = 0; i < list.size(); ++i) {
        ClosedTab t;
        t.url = list.at(i)[L"url"].asString();
        t.title = list.at(i)[L"title"].asString();
        t.index = list.at(i)[L"index"].asInt(0);
        if (!t.url.empty()) out.push_back(t);
    }
    return out;
}

void writeClosed(const std::vector<ClosedTab>& closed) {
    json::Value list = json::Value::array();
    for (auto& t : closed) {
        json::Value row = json::Value::object();
        row.set(L"url", t.url);
        row.set(L"title", t.title);
        row.set(L"index", t.index);
        list.push(std::move(row));
    }
    store::writeText(store::file(L"closed.json"), list.dump());
}

}  // namespace session
