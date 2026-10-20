// The desktop interface's translator (ui/i18n.js translator()) in C++, for the texts the in-game
// window writes while it runs (statuses, tables, notifications): the same dictionary
// (ui/i18n-<language>.json, embedded in ui.json), the same templates, number and sentence rules,
// so a text reads the same in the game as in the app. Texts known when the window is built are
// translated at build time by the app's own translator (ui.json "i18n"); this is for the rest.
#pragma once
#include "ingame_json.hpp"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ep::ingame::i18n {

class Translator {
public:
    // dictionary: {"English": "translation", "{0} settings": "{0} ayar", ...} in file order
    Translator(const json::Value& dictionary, bool decimalComma);
    // translate(text) of i18n.js: the translation, or nothing when the text has none
    std::optional<std::string> Translate(const std::string& text) const;

private:
    struct Template {
        std::vector<std::u32string> parts;  // the literal text around each {n}: parts.size() == order.size() + 1
        std::vector<int> order;             // the {n} of each group, left to right
        std::string target;
        std::u32string first;
        bool numeric = false;               // little text of its own: takes numbers only
    };
    std::unordered_map<std::string, std::string> exact_;
    std::vector<Template> templates_;
    bool decimalComma_;
    mutable int nesting_ = 0;

    std::optional<std::u32string> Translate(const std::u32string& text) const;
    std::optional<std::u32string> Lookup(const std::u32string& core) const;
    std::optional<std::u32string> Phrase(const std::u32string& core) const;
    std::optional<std::u32string> Parts(const std::u32string& text, const std::u32string& separator) const;
    std::optional<std::u32string> Sentences(const std::u32string& core) const;
    std::u32string Decimal(const std::u32string& value) const;
    const std::string* Exact(const std::u32string& key) const;
};

std::u32string Wide(const std::string& utf8);
std::string Narrow(const std::u32string& text);

}  // namespace ep::ingame::i18n
