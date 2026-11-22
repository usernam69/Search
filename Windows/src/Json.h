// A small JSON reader and writer. The settings, History, bookmarks and the
// session are small JSON files, as they are on the Mac, and this is all the
// JSON the app needs to read and write them.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace json {

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b) : kind_(Kind::Bool), bool_(b) {}
    Value(double n) : kind_(Kind::Number), number_(n) {}
    Value(int n) : kind_(Kind::Number), number_((double)n) {}
    Value(const wchar_t* s) : kind_(Kind::String), string_(s) {}
    Value(std::wstring s) : kind_(Kind::String), string_(std::move(s)) {}

    static Value array() { Value v; v.kind_ = Kind::Array; return v; }
    static Value object() { Value v; v.kind_ = Kind::Object; return v; }

    Kind kind() const { return kind_; }
    bool isNull() const { return kind_ == Kind::Null; }
    bool isObject() const { return kind_ == Kind::Object; }
    bool isArray() const { return kind_ == Kind::Array; }
    bool isString() const { return kind_ == Kind::String; }
    bool isNumber() const { return kind_ == Kind::Number; }
    bool isBool() const { return kind_ == Kind::Bool; }

    bool asBool(bool fallback = false) const { return kind_ == Kind::Bool ? bool_ : fallback; }
    double asNumber(double fallback = 0) const { return kind_ == Kind::Number ? number_ : fallback; }
    int asInt(int fallback = 0) const { return kind_ == Kind::Number ? (int)number_ : fallback; }
    std::wstring asString(const std::wstring& fallback = L"") const {
        return kind_ == Kind::String ? string_ : fallback;
    }

    // Objects. A missing key reads as null, so a file written by a newer
    // version, or an older one, never stops the reader.
    const Value& operator[](const std::wstring& key) const;
    bool has(const std::wstring& key) const;
    void set(const std::wstring& key, Value v);
    const std::vector<std::pair<std::wstring, Value>>& members() const { return members_; }

    // Arrays.
    size_t size() const { return kind_ == Kind::Array ? items_.size() : 0; }
    const Value& at(size_t i) const;
    void push(Value v);

    std::wstring dump() const;

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0;
    std::wstring string_;
    std::vector<Value> items_;
    std::vector<std::pair<std::wstring, Value>> members_;
};

// Nothing, for text that isn't JSON.
std::unique_ptr<Value> parse(const std::wstring& text);

}  // namespace json
