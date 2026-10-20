// Data-only presentation/composition used by the native collection and Echo browsers.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_json.hpp"
#include <windows.h>
#ifdef small
#undef small
#endif
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace ep::ingame::model {
using J = json::Value;
inline J Text(std::string v) { J j; j.type = J::Type::String; j.text = std::move(v); return j; }
inline J Number(double v) { J j; j.type = J::Type::Number; j.number = v; return j; }
inline J Flag(bool v) { J j; j.type = J::Type::Bool; j.boolean = v; return j; }
inline J List(std::vector<J> v = {}) { J j; j.type = J::Type::Array; j.items = std::move(v); return j; }
inline J Object() { J j; j.type = J::Type::Object; return j; }
inline void Put(J& j, const std::string& key, J value) {
    for (auto& [k, v] : j.members) if (k == key) { v = std::move(value); return; }
    j.members.emplace_back(key, std::move(value));
}
inline std::string Id(const J& v) {
    if (v.IsString()) return v.text;
    if (v.IsNumber() && std::isfinite(v.number)) { char b[64]; const auto r = std::to_chars(b, b + 64, v.number); return {b, r.ptr}; }
    return "";
}
inline std::string Quote(const std::string& text) {
    const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + '"';
}
inline std::string Pack(const J& j) {
    if (j.IsNull()) return "null";
    if (j.IsString()) return Quote(j.text);
    if (j.IsBool()) return j.boolean ? "true" : "false";
    if (j.IsNumber()) { if (!std::isfinite(j.number)) throw std::runtime_error("Invalid stored number."); return Id(j); }
    std::string out = j.IsArray() ? "[" : "{";
    if (j.IsArray()) for (const auto& v : j.items) out += (out.size() == 1 ? "" : ",") + Pack(v);
    else for (const auto& [key, v] : j.members) out += (out.size() == 1 ? "" : ",") + Quote(key) + ":" + Pack(v);
    return out + (j.IsArray() ? "]" : "}");
}
inline std::string Fold(const std::string& text) {
    if (text.empty()) return "";
    std::wstring wide(MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    std::wstring lower(LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), static_cast<int>(wide.size()), lower.data(), static_cast<int>(lower.size()), nullptr, nullptr, 0);
    std::string out(WideCharToMultiByte(CP_UTF8, 0, lower.data(), static_cast<int>(lower.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, lower.data(), static_cast<int>(lower.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
    return out;
}
inline bool On(const J& j) { return j.IsBool() && j.boolean; }
inline bool SameOwner(const J& a, const J& b) { return !Id(a["player"]["id"]).empty() && Id(a["player"]["id"]) == Id(b["player"]["id"]); }
struct Scan {
    J first, result;
    std::vector<J> rows;
    int offset = 0, pages = 0;
    double maxMs = 0;
    bool complete = false;
    void Accept(const J& page) {
        if (complete || !On(page["ok"])) throw std::runtime_error(page["error"].Str("Could not read the loaded stash."));
        const auto& total = page["total"];
        if (Id(page["player"]["id"]).empty() || page["scope"].Str().empty() || Id(page["revision"]).empty() ||
            !total.IsNumber() || total.number != std::floor(total.number) || total.number < 0 || total.number > 65536)
            throw std::runtime_error("Invalid collection snapshot identity.");
        if (!pages) first = page;
        else if (!SameOwner(first, page) || Pack(first["scope"]) != Pack(page["scope"]) ||
                 Pack(first["revision"]) != Pack(page["revision"]) || first["total"].Num() != total.number)
            throw std::runtime_error("Items or the loaded character changed during the scan. Refresh again.");
        if (++pages > 656 || !page["offset"].IsNumber() || page["offset"].Num() != offset || !page["items"].IsArray() || page["items"].items.size() > 100)
            throw std::runtime_error("Invalid collection page.");
        rows.insert(rows.end(), page["items"].items.begin(), page["items"].items.end());
        maxMs = std::max(maxMs, page["readMs"].Num());
        if (page["next"].IsNull()) {
            if (rows.size() != static_cast<size_t>(first["total"].Num())) throw std::runtime_error("Incomplete collection scan; no partial result was displayed.");
            result = first; Put(result, "items", List(std::move(rows))); Put(result, "offset", Number(0)); Put(result, "next", J{});
            Put(result, "pages", Number(pages)); Put(result, "maxPageMs", Number(maxMs)); complete = true;
        } else {
            const double next = page["next"].Num(-1);
            if (!page["next"].IsNumber() || next != offset + page["items"].items.size() || next <= offset || next >= total.number)
                throw std::runtime_error("Invalid collection continuation.");
            offset = static_cast<int>(next);
        }
    }
};
inline J Atlas(const J& catalog, const J& stash) {
    if (!On(catalog["ok"]) || !catalog["items"].IsArray() || !SameOwner(catalog, stash)) throw std::runtime_error("The loaded character changed during the scan. Refresh again.");
    std::map<std::string, std::vector<J>> copies;
    for (const auto& item : stash["items"].items) if (!item["uniqueId"].IsNull()) copies[Id(item["uniqueId"])].push_back(item);
    J result = stash; std::vector<J> rows; std::set<std::string> seen; int owned = 0;
    for (auto item : catalog["items"].items) {
        const std::string id = Id(item["id"]);
        if (id.empty() || !seen.insert(id).second) throw std::runtime_error("Duplicate unique catalog identity.");
        double count = 0, best = -1;
        for (const auto& copy : copies[id]) {
            count += copy["quantity"].Num(1);
            if (copy["kind"].Str() == "Unique" && copy["weaversWill"].Num() == 0) best = std::max(best, copy["lp"].Num());
        }
        Put(item, "count", Number(count)); Put(item, "bestLP", best < 0 ? J{} : Number(best)); Put(item, "copies", List(copies[id]));
        rows.push_back(std::move(item)); owned += count > 0;
    }
    Put(result, "items", List(std::move(rows))); Put(result, "total", Number(result["items"].items.size())); Put(result, "owned", Number(owned));
    return result;
}
inline J Groups(const J& stash) {
    std::map<std::string, std::vector<J>> groups;
    for (const auto& item : stash["items"].items) {
        const std::string key = item["uniqueId"].IsNull() ? "base:" + Id(item["type"]) + ":" + Id(item["subType"]) : "unique:" + Id(item["uniqueId"]);
        groups[key].push_back(item);
    }
    J result = stash; std::vector<J> rows; int duplicates = 0;
    for (const auto& [key, copies] : groups) {
        J row = Object(); const bool unique = key.rfind("unique:", 0) == 0;
        Put(row, "key", Text(key)); Put(row, "name", Text(unique ? copies[0]["name"].Str() : copies[0]["baseType"].Str() + " · " + copies[0]["name"].Str()));
        Put(row, "unique", Flag(unique)); Put(row, "count", Number(copies.size())); Put(row, "items", List(copies));
        rows.push_back(std::move(row)); duplicates += unique && copies.size() > 1;
    }
    Put(result, "groups", List(std::move(rows))); Put(result, "duplicateGroups", Number(duplicates)); return result;
}
inline const char* Reward(int n) {
    static constexpr const char* names[]{"Special Echo", "Gold", "Experience", "Rare item", "Rare items", "Unique item", "Unique items", "Exalted item", "Exalted items", "Affix shards", "Runes", "Glyphs", "Idols", "Set items", "Timeline key", "Arena key", "Woven Echoes", "Dungeon keys", "Memory Amber"};
    return n >= 0 && n < 19 ? names[n] : "Special reward";
}
inline bool EchoMatch(const J& e, const std::string& query, const std::string& reward, const std::string& state) {
    return Fold(e["name"].Str() + " " + e["reward"].Str() + " " + Reward(static_cast<int>(e["rewardType"].Num()))).find(Fold(query)) != std::string::npos &&
        (reward == "all" || Id(e["rewardType"]) == reward) &&
        (state == "all" || (state == "ready" && On(e["runnable"])) || (state == "pending" && !On(e["completed"])) || (state == "completed" && On(e["completed"])));
}
} // namespace ep::ingame::model
