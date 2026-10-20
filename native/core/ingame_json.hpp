// A small JSON reader for the in-game interface: its own embedded data (ui.json) and the replies of
// the core's read commands. Pure C++ (no Unity), covered by native/tests/ingame_json_test.cpp.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ep::ingame::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> members;

    bool IsNull() const { return type == Type::Null; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    const Value* Find(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& m : members) if (m.first == key) return &m.second;
        return nullptr;
    }
    const Value& operator[](std::string_view key) const {
        static const Value none;
        const Value* v = Find(key);
        return v ? *v : none;
    }
    // "settings.zoom.value": members only (no array indexes).
    const Value* Path(std::string_view dotted) const {
        const Value* at = this;
        while (at && !dotted.empty()) {
            const size_t dot = dotted.find('.');
            at = at->Find(dotted.substr(0, dot));
            dotted = dot == std::string_view::npos ? std::string_view() : dotted.substr(dot + 1);
        }
        return at;
    }
    std::string Str(std::string_view fallback = {}) const { return type == Type::String ? text : std::string(fallback); }
    double Num(double fallback = 0) const { return type == Type::Number ? number : type == Type::Bool ? (boolean ? 1 : 0) : fallback; }
};

namespace detail {
struct Reader {
    std::string_view s;
    size_t i = 0;
    std::string error;
    int depth = 0;

    void Space() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool Fail(const char* what) { if (error.empty()) error = std::string(what) + " at " + std::to_string(i); return false; }
    bool Literal(const char* word) {
        const std::string_view w(word);
        if (s.substr(i, w.size()) != w) return Fail("unexpected token");
        i += w.size();
        return true;
    }
    static void Utf8(uint32_t cp, std::string& out) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(uint32_t& out) {
        if (i + 4 > s.size()) return Fail("short \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        return true;
    }
    bool String(std::string& out) {
        if (i >= s.size() || s[i] != '"') return Fail("expected string");
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return Fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t low = 0;
                    if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return Fail("lone surrogate");
                    i += 2;
                    if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF) return Fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) return Fail("lone surrogate");
                Utf8(cp, out);
                break;
            }
            default: return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }
    bool Number(double& out) {
        const size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return Fail("bad number");
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        const std::string text(s.substr(start, i - start));
        char* end = nullptr;
        out = std::strtod(text.c_str(), &end);
        if (!end || *end) return Fail("bad number");
        return true;
    }
    bool Parse(Value& v) {
        if (++depth > 64) return Fail("nested too deeply");
        Space();
        if (i >= s.size()) return Fail("unexpected end");
        bool ok = true;
        const char c = s[i];
        if (c == '{') {
            v.type = Value::Type::Object;
            ++i; Space();
            if (i < s.size() && s[i] == '}') ++i;
            else for (;;) {
                Space();
                std::pair<std::string, Value> m;
                if (!String(m.first)) { ok = false; break; }
                Space();
                if (i >= s.size() || s[i] != ':') { ok = Fail("expected ':'"); break; }
                ++i;
                if (!Parse(m.second)) { ok = false; break; }
                v.members.push_back(std::move(m));
                Space();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                ok = Fail("expected ',' or '}'"); break;
            }
        } else if (c == '[') {
            v.type = Value::Type::Array;
            ++i; Space();
            if (i < s.size() && s[i] == ']') ++i;
            else for (;;) {
                Value item;
                if (!Parse(item)) { ok = false; break; }
                v.items.push_back(std::move(item));
                Space();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                ok = Fail("expected ',' or ']'"); break;
            }
        } else if (c == '"') { v.type = Value::Type::String; ok = String(v.text); }
        else if (c == 't') { v.type = Value::Type::Bool; v.boolean = true; ok = Literal("true"); }
        else if (c == 'f') { v.type = Value::Type::Bool; ok = Literal("false"); }
        else if (c == 'n') { ok = Literal("null"); }
        else { v.type = Value::Type::Number; ok = Number(v.number); }
        --depth;
        return ok;
    }
};
}  // namespace detail

// Parses one complete JSON text; on failure `out` is unspecified and `error` says where.
inline bool Parse(std::string_view text, Value& out, std::string* error = nullptr) {
    detail::Reader r{text};
    out = Value{};
    bool ok = r.Parse(out);
    if (ok) { r.Space(); if (r.i != text.size()) ok = r.Fail("trailing characters"); }
    if (!ok && error) *error = r.error;
    return ok;
}

}  // namespace ep::ingame::json
