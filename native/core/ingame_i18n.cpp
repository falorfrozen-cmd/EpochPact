// See ingame_i18n.hpp. Each function follows its namesake in ui/i18n.js translator(); the regular
// expressions there are written out by hand here (JavaScript semantics: \d is 0-9, \s is the
// Unicode white space, [A-Za-z] is ASCII, \p{L} and \p{N} are letters and numbers).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_i18n.hpp"
#include <windows.h>
#include <algorithm>

namespace ep::ingame::i18n {
namespace {
bool Space(char32_t c) {  // \s
    return c == U'\t' || c == U'\n' || c == U'\v' || c == U'\f' || c == U'\r' || c == U' ' || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}
bool Digit(char32_t c) { return c >= U'0' && c <= U'9'; }
bool Ascii(char32_t c) { return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z'); }
bool HasAscii(const std::u32string& s) { return std::any_of(s.begin(), s.end(), Ascii); }
WORD Type(char32_t c) {
    if (c > 0xFFFF) return 0;
    const wchar_t w = static_cast<wchar_t>(c);
    WORD t = 0;
    GetStringTypeW(CT_CTYPE1, &w, 1, &t);
    return t;
}
bool Letter(char32_t c) { return Ascii(c) || (c > 0x7F && (Type(c) & C1_ALPHA) && !(Type(c) & C1_DIGIT)); }  // \p{L}
bool Number(char32_t c) {  // \p{N}
    return Digit(c) || (c > 0x7F && (Type(c) & C1_DIGIT)) || c == 0xB2 || c == 0xB3 || c == 0xB9 || (c >= 0xBC && c <= 0xBE) || (c >= 0x2150 && c <= 0x218B) ||
           (c >= 0x2460 && c <= 0x249B) || (c >= 0x2070 && c <= 0x2079) || (c >= 0x2080 && c <= 0x2089);
}
bool Sign(char32_t c) { return c == U'+' || c == U'-' || c == 0x2212; }  // [+\-−]
std::u32string Trim(const std::u32string& s) {
    size_t a = 0, b = s.size();
    while (a < b && Space(s[a])) ++a;
    while (b > a && Space(s[b - 1])) --b;
    return s.substr(a, b - a);
}
// text.trim().replace(/\s+/g,' ')
std::u32string Collapse(const std::u32string& s) {
    std::u32string out;
    bool space = false;
    for (char32_t c : Trim(s)) {
        if (Space(c)) { space = true; continue; }
        if (space) out += U' ';
        space = false;
        out += c;
    }
    return out;
}
bool StartsWith(const std::u32string& s, const std::u32string& prefix) { return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin()); }
std::vector<std::u32string> Split(const std::u32string& s, const std::u32string& separator) {
    std::vector<std::u32string> out;
    size_t at = 0;
    for (size_t next; (next = s.find(separator, at)) != std::u32string::npos; at = next + separator.size()) out.push_back(s.substr(at, next - at));
    out.push_back(s.substr(at));
    return out;
}
std::u32string Join(const std::vector<std::u32string>& list, const std::u32string& separator) {
    std::u32string out;
    for (size_t i = 0; i < list.size(); ++i) out += (i ? separator : std::u32string()) + list[i];
    return out;
}
// ^\s*[:·]?\s*[+\-−]?\d[\d.,]*\s*%?$ : the number after a text (the after rule's second group)
bool NumberTail(const std::u32string& s, size_t i) {
    while (i < s.size() && Space(s[i])) ++i;
    if (i < s.size() && (s[i] == U':' || s[i] == 0xB7)) ++i;
    while (i < s.size() && Space(s[i])) ++i;
    if (i < s.size() && Sign(s[i])) ++i;
    if (i >= s.size() || !Digit(s[i])) return false;
    ++i;
    while (i < s.size() && (Digit(s[i]) || s[i] == U'.' || s[i] == U',')) ++i;
    while (i < s.size() && Space(s[i])) ++i;
    if (i < s.size() && s[i] == U'%') ++i;
    return i == s.size();
}
// The groups of ^lit0(.*?)lit1(.*?)…litN$ (lazy, the first match a backtracking engine finds).
bool Match(const std::vector<std::u32string>& parts, const std::u32string& s, size_t part, size_t at, std::vector<std::u32string>& groups) {
    if (part + 1 == parts.size()) return false;  // unreachable: the last literal is checked below
    // group `part` starts at `at`; try its lengths from the shortest
    const std::u32string& next = parts[part + 1];
    const bool last = part + 2 == parts.size();
    for (size_t end = at; end <= s.size(); ++end) {
        if (last) {
            // the final literal must close the text
            if (s.size() - end != next.size() || s.compare(end, next.size(), next) != 0) continue;
            groups.push_back(s.substr(at, end - at));
            return true;
        }
        if (s.compare(end, next.size(), next) != 0 || end + next.size() > s.size()) continue;
        groups.push_back(s.substr(at, end - at));
        if (Match(parts, s, part + 1, end + next.size(), groups)) return true;
        groups.pop_back();
    }
    return false;
}
}  // namespace

std::u32string Wide(const std::string& utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char c = utf8[i];
        char32_t cp = c;
        size_t n = 1;
        if (c >= 0xF0 && i + 3 < utf8.size() + 0 && i + 3 <= utf8.size() - 1 + 1) { cp = c & 0x07; n = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        if (i + n > utf8.size()) { out += 0xFFFD; break; }
        for (size_t k = 1; k < n; ++k) cp = cp << 6 | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        out += cp;
        i += n;
    }
    return out;
}
std::string Narrow(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | c >> 6); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | c >> 12); out += static_cast<char>(0x80 | (c >> 6 & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else {
            out += static_cast<char>(0xF0 | c >> 18);
            out += static_cast<char>(0x80 | (c >> 12 & 0x3F));
            out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// compile(): exact texts, and templates with their literal prefix (longest first).
Translator::Translator(const json::Value& dictionary, bool decimalComma) : decimalComma_(decimalComma) {
    for (const auto& [source, target] : dictionary.members) {
        if (!target.IsString() || target.text.empty()) continue;
        const std::u32string s = Wide(source);
        Template t;
        std::u32string literal;
        bool placeholder = false;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == U'{') {
                size_t j = i + 1;
                while (j < s.size() && Digit(s[j])) ++j;
                if (j > i + 1 && j < s.size() && s[j] == U'}') {
                    t.parts.push_back(literal);
                    literal.clear();
                    t.order.push_back(std::stoi(Narrow(s.substr(i + 1, j - i - 1))));
                    placeholder = true;
                    i = j;
                    continue;
                }
            }
            literal += s[i];
        }
        if (!placeholder) { exact_.emplace(source, target.text); continue; }
        t.parts.push_back(literal);
        t.target = target.text;
        t.first = t.parts.front();
        int letters = 0;
        for (const auto& part : t.parts) letters += static_cast<int>(std::count_if(part.begin(), part.end(), Ascii));
        t.numeric = letters < 6;
        templates_.push_back(std::move(t));
    }
    std::stable_sort(templates_.begin(), templates_.end(), [](const Template& a, const Template& b) { return a.first.size() > b.first.size(); });
}
const std::string* Translator::Exact(const std::u32string& key) const {
    auto it = exact_.find(Narrow(key));
    return it == exact_.end() ? nullptr : &it->second;
}
std::u32string Translator::Decimal(const std::u32string& value) const {
    if (!decimalComma_) return value;
    // ^\s*[+\-−]?\d+\.\d+\s*$
    size_t i = 0;
    while (i < value.size() && Space(value[i])) ++i;
    if (i < value.size() && Sign(value[i])) ++i;
    const size_t a = i;
    while (i < value.size() && Digit(value[i])) ++i;
    if (i == a || i >= value.size() || value[i] != U'.') return value;
    const size_t dot = i++;
    const size_t b = i;
    while (i < value.size() && Digit(value[i])) ++i;
    if (i == b) return value;
    while (i < value.size() && Space(value[i])) ++i;
    if (i != value.size()) return value;
    std::u32string out = value;
    out[dot] = U',';
    return out;
}
std::optional<std::u32string> Translator::Lookup(const std::u32string& core) const {
    if (const std::string* hit = Exact(core)) return Wide(*hit);
    for (const auto& t : templates_) {
        if (!t.first.empty() && !StartsWith(core, t.first)) continue;
        if (!StartsWith(core, t.parts.front())) continue;
        std::vector<std::u32string> groups;
        if (!Match(t.parts, core, 0, t.parts.front().size(), groups)) continue;
        if (t.numeric && std::any_of(groups.begin(), groups.end(), [](const std::u32string& v) { return std::none_of(v.begin(), v.end(), Digit); })) continue;
        std::vector<std::optional<std::u32string>> values(32);
        for (size_t i = 0; i < groups.size() && i < t.order.size(); ++i) {
            const std::u32string& value = groups[i];
            std::optional<std::u32string> inner;
            if (nesting_ < 2 && HasAscii(value)) {
                ++nesting_;
                inner = Translate(value);
                --nesting_;
            }
            if (t.order[i] >= 0 && t.order[i] < 32) values[t.order[i]] = inner ? *inner : Decimal(value);
        }
        // target.replace(/\{(\d+)\}/g, values[n] ?? '')
        const std::u32string target = Wide(t.target);
        std::u32string out;
        for (size_t i = 0; i < target.size(); ++i) {
            if (target[i] == U'{') {
                size_t j = i + 1;
                while (j < target.size() && Digit(target[j])) ++j;
                if (j > i + 1 && j < target.size() && target[j] == U'}') {
                    const int n = std::stoi(Narrow(target.substr(i + 1, j - i - 1)));
                    if (n >= 0 && n < 32 && values[n]) out += *values[n];
                    i = j;
                    continue;
                }
            }
            out += target[i];
        }
        return out;
    }
    return std::nullopt;
}
std::optional<std::u32string> Translator::Phrase(const std::u32string& core) const {
    if (auto out = Lookup(core)) return out;
    // before: ^([+\-−]?\d[\d.,]*\s*[%×]?\s*)([^\d].*)$  ("12 shard types")
    {
        size_t i = 0;
        if (i < core.size() && Sign(core[i])) ++i;
        if (i < core.size() && Digit(core[i])) {
            ++i;
            while (i < core.size() && (Digit(core[i]) || core[i] == U'.' || core[i] == U',')) ++i;
            while (i < core.size() && Space(core[i])) ++i;
            if (i < core.size() && (core[i] == U'%' || core[i] == 0xD7)) ++i;
            while (i < core.size() && Space(core[i])) ++i;
            // the greedy runs give back characters only to let [^\d] match; a digit there fails
            if (i < core.size() && !Digit(core[i]))
                if (auto rest = Lookup(core.substr(i))) return core.substr(0, i) + *rest;
        }
    }
    // after: ^(.*[^\d\s])(\s*[:·]?\s*[+\-−]?\d[\d.,]*\s*%?)$  ("Rule 45")
    for (size_t i = core.size(); i-- > 0;) {
        if (Digit(core[i]) || Space(core[i]) || !NumberTail(core, i + 1)) continue;
        if (auto rest = Lookup(core.substr(0, i + 1))) return *rest + core.substr(i + 1);
        break;
    }
    // an icon in front: ^([^\p{L}\p{N}"“(+\-−]+)(\p{L}.*)$
    {
        size_t k = 0;
        while (k < core.size() && !Letter(core[k]) && !Number(core[k]) && core[k] != U'"' && core[k] != 0x201C && core[k] != U'(' && !Sign(core[k])) ++k;
        if (k > 0 && k < core.size() && Letter(core[k]))
            if (auto rest = Lookup(core.substr(k))) return core.substr(0, k) + *rest;
    }
    // a known label, then game or player data: ^([^:]{2,60}:)(\s+.*)$
    {
        const size_t colon = core.find(U':');
        if (colon != std::u32string::npos && colon >= 2 && colon <= 60 && colon + 1 < core.size() && Space(core[colon + 1]))
            if (const std::string* label = Exact(core.substr(0, colon + 1))) return Wide(*label) + core.substr(colon + 1);
    }
    return std::nullopt;
}
std::optional<std::u32string> Translator::Parts(const std::u32string& text, const std::u32string& separator) const {
    std::vector<std::u32string> list = Split(text, separator);
    bool changed = false;
    for (auto& part : list) {
        if (auto t = Translate(part)) { part = *t; changed = true; }
    }
    if (!changed) return std::nullopt;
    return Join(list, separator);
}
// split(/(?<=[.!?…])\s+(?=[A-Z“"(])/), then the longest known runs of whole sentences
std::optional<std::u32string> Translator::Sentences(const std::u32string& core) const {
    std::vector<std::u32string> list;
    size_t start = 0;
    for (size_t i = 1; i < core.size(); ++i) {
        if (!Space(core[i])) continue;
        const char32_t before = core[i - 1];
        if (before != U'.' && before != U'!' && before != U'?' && before != 0x2026) continue;
        size_t j = i;
        while (j < core.size() && Space(core[j])) ++j;
        if (j >= core.size()) break;
        const char32_t after = core[j];
        if (!((after >= U'A' && after <= U'Z') || after == 0x201C || after == U'"' || after == U'(')) continue;
        list.push_back(core.substr(start, i - start));
        start = j;
        i = j;
    }
    list.push_back(core.substr(start));
    if (list.size() < 2) return std::nullopt;
    std::vector<std::u32string> out;
    bool changed = false;
    for (size_t i = 0; i < list.size();) {
        size_t j = list.size();
        std::optional<std::u32string> hit;
        for (; j > i; --j) {
            std::vector<std::u32string> run(list.begin() + static_cast<std::ptrdiff_t>(i), list.begin() + static_cast<std::ptrdiff_t>(j));
            hit = Phrase(Join(run, U" "));
            if (hit) break;
        }
        if (hit) { out.push_back(*hit); changed = true; i = j; }
        else { out.push_back(list[i]); ++i; }
    }
    if (!changed) return std::nullopt;
    return Join(out, U" ");
}
std::optional<std::u32string> Translator::Translate(const std::u32string& text) const {
    const std::u32string core = Collapse(text);
    if (core.empty() || !HasAscii(core)) return std::nullopt;
    size_t a = 0, b = text.size();
    while (a < b && Space(text[a])) ++a;
    while (b > a && Space(text[b - 1])) --b;
    const std::u32string lead = text.substr(0, a), trail = text.substr(b);
    // several lines: a known whole text, else line by line
    if (Trim(text).find(U'\n') != std::u32string::npos) {
        if (const std::string* whole = Exact(core)) return lead + Wide(*whole) + trail;
        if (auto lines = Parts(text, U"\n")) return lines;
    }
    if (auto out = Phrase(core)) return lead + *out + trail;
    if (text.find(U" · ") != std::u32string::npos) return Parts(text, U" · ");
    if (auto joined = Sentences(core)) return lead + *joined + trail;
    return std::nullopt;
}
std::optional<std::string> Translator::Translate(const std::string& text) const {
    auto out = Translate(Wide(text));
    if (!out) return std::nullopt;
    return Narrow(*out);
}

}  // namespace ep::ingame::i18n
