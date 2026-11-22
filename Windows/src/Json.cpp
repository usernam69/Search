#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {

static const Value kNull;

const Value& Value::operator[](const std::wstring& key) const {
    for (auto& member : members_) {
        if (member.first == key) return member.second;
    }
    return kNull;
}

bool Value::has(const std::wstring& key) const {
    for (auto& member : members_) {
        if (member.first == key) return true;
    }
    return false;
}

void Value::set(const std::wstring& key, Value v) {
    if (kind_ != Kind::Object) {
        kind_ = Kind::Object;
        members_.clear();
    }
    for (auto& member : members_) {
        if (member.first == key) {
            member.second = std::move(v);
            return;
        }
    }
    members_.emplace_back(key, std::move(v));
}

const Value& Value::at(size_t i) const {
    if (kind_ != Kind::Array || i >= items_.size()) return kNull;
    return items_[i];
}

void Value::push(Value v) {
    if (kind_ != Kind::Array) {
        kind_ = Kind::Array;
        items_.clear();
    }
    items_.push_back(std::move(v));
}

// ---- writing ----

static void escape(const std::wstring& s, std::wstring& out) {
    out += L'"';
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += L'"';
}

static void dumpInto(const Value& v, std::wstring& out) {
    switch (v.kind()) {
        case Value::Kind::Null: out += L"null"; break;
        case Value::Kind::Bool: out += v.asBool() ? L"true" : L"false"; break;
        case Value::Kind::Number: {
            double n = v.asNumber();
            wchar_t buf[64];
            if (n == std::floor(n) && std::fabs(n) < 1e15) {
                swprintf(buf, 64, L"%.0f", n);
            } else {
                swprintf(buf, 64, L"%.17g", n);
            }
            out += buf;
            break;
        }
        case Value::Kind::String: escape(v.asString(), out); break;
        case Value::Kind::Array: {
            out += L'[';
            for (size_t i = 0; i < v.size(); ++i) {
                if (i) out += L',';
                dumpInto(v.at(i), out);
            }
            out += L']';
            break;
        }
        case Value::Kind::Object: {
            out += L'{';
            bool first = true;
            for (auto& member : v.members()) {
                if (!first) out += L',';
                first = false;
                escape(member.first, out);
                out += L':';
                dumpInto(member.second, out);
            }
            out += L'}';
            break;
        }
    }
}

std::wstring Value::dump() const {
    std::wstring out;
    out.reserve(256);
    dumpInto(*this, out);
    return out;
}

// ---- reading ----

namespace {

struct Reader {
    const std::wstring& text;
    size_t at = 0;
    bool ok = true;

    void skip() {
        while (at < text.size() && (text[at] == L' ' || text[at] == L'\t' || text[at] == L'\n' || text[at] == L'\r')) {
            ++at;
        }
    }

    bool eat(wchar_t c) {
        skip();
        if (at < text.size() && text[at] == c) {
            ++at;
            return true;
        }
        return false;
    }

    void fail() {
        ok = false;
        at = text.size();
    }

    Value parse() {
        skip();
        if (at >= text.size()) {
            fail();
            return Value();
        }
        wchar_t c = text[at];
        if (c == L'{') return object();
        if (c == L'[') return array();
        if (c == L'"') return Value(string());
        if (c == L't' || c == L'f') return boolean();
        if (c == L'n') return literalNull();
        return number();
    }

    Value literalNull() {
        if (text.compare(at, 4, L"null") == 0) {
            at += 4;
            return Value();
        }
        fail();
        return Value();
    }

    Value boolean() {
        if (text.compare(at, 4, L"true") == 0) {
            at += 4;
            return Value(true);
        }
        if (text.compare(at, 5, L"false") == 0) {
            at += 5;
            return Value(false);
        }
        fail();
        return Value();
    }

    Value number() {
        size_t start = at;
        if (at < text.size() && text[at] == L'-') ++at;
        while (at < text.size() && text[at] >= L'0' && text[at] <= L'9') ++at;
        if (at < text.size() && text[at] == L'.') {
            ++at;
            while (at < text.size() && text[at] >= L'0' && text[at] <= L'9') ++at;
        }
        if (at < text.size() && (text[at] == L'e' || text[at] == L'E')) {
            ++at;
            if (at < text.size() && (text[at] == L'+' || text[at] == L'-')) ++at;
            while (at < text.size() && text[at] >= L'0' && text[at] <= L'9') ++at;
        }
        if (at == start) {
            fail();
            return Value();
        }
        return Value(_wtof(text.substr(start, at - start).c_str()));
    }

    std::wstring string() {
        if (!eat(L'"')) {
            fail();
            return L"";
        }
        std::wstring out;
        while (at < text.size()) {
            wchar_t c = text[at++];
            if (c == L'"') return out;
            if (c != L'\\') {
                out += c;
                continue;
            }
            if (at >= text.size()) break;
            wchar_t e = text[at++];
            switch (e) {
                case L'"': out += L'"'; break;
                case L'\\': out += L'\\'; break;
                case L'/': out += L'/'; break;
                case L'b': out += L'\b'; break;
                case L'f': out += L'\f'; break;
                case L'n': out += L'\n'; break;
                case L'r': out += L'\r'; break;
                case L't': out += L'\t'; break;
                case L'u': {
                    if (at + 4 > text.size()) {
                        fail();
                        return out;
                    }
                    unsigned code = (unsigned)wcstoul(text.substr(at, 4).c_str(), nullptr, 16);
                    at += 4;
                    // A surrogate pair, as an emoji in a tab's name is.
                    if (code >= 0xD800 && code <= 0xDBFF && text.compare(at, 2, L"\\u") == 0 &&
                        at + 6 <= text.size()) {
                        unsigned low = (unsigned)wcstoul(text.substr(at + 2, 4).c_str(), nullptr, 16);
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            at += 6;
                            unsigned full = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                            out += (wchar_t)(0xD800 + ((full - 0x10000) >> 10));
                            out += (wchar_t)(0xDC00 + ((full - 0x10000) & 0x3FF));
                            break;
                        }
                    }
                    out += (wchar_t)code;
                    break;
                }
                default: out += e;
            }
        }
        fail();
        return out;
    }

    Value array() {
        eat(L'[');
        Value v = Value::array();
        skip();
        if (eat(L']')) return v;
        for (;;) {
            v.push(parse());
            if (!ok) return v;
            skip();
            if (eat(L']')) return v;
            if (!eat(L',')) {
                fail();
                return v;
            }
        }
    }

    Value object() {
        eat(L'{');
        Value v = Value::object();
        skip();
        if (eat(L'}')) return v;
        for (;;) {
            skip();
            std::wstring key = string();
            if (!ok) return v;
            skip();
            if (!eat(L':')) {
                fail();
                return v;
            }
            v.set(key, parse());
            if (!ok) return v;
            skip();
            if (eat(L'}')) return v;
            if (!eat(L',')) {
                fail();
                return v;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Value> parse(const std::wstring& text) {
    Reader r{text};
    Value v = r.parse();
    if (!r.ok) return nullptr;
    return std::make_unique<Value>(std::move(v));
}

}  // namespace json
