// An action card's form (app.js parameterFields and formAction): one field per command parameter
// in the card's 2-column form grid, the values kept per control while the window runs, checked as
// the browser checks a form (required, number, integer, limits) and written into the command.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ingame_window.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace ep::ingame::view {
namespace {
std::map<std::string, Form> forms;
std::string FieldLabel(const Control& c, const std::string& name) {
    static const std::map<std::string, std::string> labels{
        {"value", "Value"},       {"name", "Character name"},  {"level", "Level (optional)"}, {"slot", "Prophecy slot"},
        {"reward", "Reward"},     {"lens", "Lens"},             {"backup", "Operation backup"}, {"mode", "Selection"},
        {"ids", "Affixes"},       {"category", "Pickup category"}, {"template_id", "Whole Unique / Set template"},
        {"affixes", "T7 affixes"}, {"perfect", "Roll quality"}, {"modifiers", "Additional modifier lines"}};
    if (c.id == "craft_materials" && name == "value") return "Minimum quantity per material";
    auto it = labels.find(name);
    return it == labels.end() ? name : it->second;
}
bool Optional(const Param& p) { return p.type == "optional_integer" || p.type == "number_or_reset"; }
}  // namespace

Form& FormOf(const std::string& control) { return forms[control]; }
void ForgetForm(const std::string& control) { forms.erase(control); }

float ParameterFields(const Ctx& c, float x, float y, float w, const Control& ctl, const FieldChoices& choices, const FieldValue& initial, int rebuildSection,
                      const std::vector<std::string>& skip) {
    Form& form = forms[ctl.id];
    const float colW = (w - 14) / 2;
    float cy = y, rowH = 0;
    int col = 0;
    const std::string id = ctl.id;
    for (const auto& p : ctl.params) {
        if (p.name == "save_id" || std::find(skip.begin(), skip.end(), p.name) != skip.end()) continue;
        const float fx = x + col * (colW + 14);
        const std::string label = Tr(FieldLabel(ctl, p.name));
        const std::string name = p.name;
        float h = 0;
        std::vector<Choice> options = choices ? choices(p) : std::vector<Choice>{};
        if (!options.empty()) {
            // a select: the kept value, else the field's own first choice (as the browser shows it)
            std::string value = form.values.count(name) ? form.values[name] : initial ? initial(p) : std::string();
            int at = 0;
            for (size_t i = 0; i < options.size(); ++i) if (options[i].value == value) at = static_cast<int>(i);
            form.values[name] = options[at].value;
            std::vector<std::string> labels;
            for (const auto& o : options) labels.push_back(o.label);
            h = SelectField(c, fx, cy, colW, label, labels, at, [id, name, options, rebuildSection](int i) {
                FormOf(id).values[name] = options[i].value;
                FormOf(id).changed = name;
                if (rebuildSection >= 0) Rebuild(rebuildSection);
            }, id + "-" + name);
        } else {
            const std::string value = form.values.count(name) ? form.values[name] : initial ? initial(p) : std::string();
            form.values[name] = value;
            const bool number = p.type != "single_token";
            const std::string hint = p.type == "number_or_reset" ? Tr("Normal chance (blank resets)") : "";
            h = InputField(c, fx, cy, colW, label, value, hint, number, [id, name](const std::string& text) { FormOf(id).values[name] = text; }, id + "-" + name);
        }
        rowH = std::max(rowH, h);
        if (++col == 2) {
            col = 0;
            cy += rowH + 14;
            rowH = 0;
        }
    }
    if (col) cy += rowH;
    else if (cy > y) cy -= 14;
    return cy - y;
}

// The form's command line, or why it cannot be sent (the browser's own form check, in plain words).
bool FormCommand(const Control& ctl, std::string* line, std::string* error, const std::map<std::string, std::string>& extra) {
    const Form& form = forms[ctl.id];
    std::string out = ctl.command;
    for (const auto& p : ctl.params) {
        if (p.name == "save_id") continue;
        std::string value;
        if (auto it = extra.find(p.name); it != extra.end()) value = it->second;
        else if (auto f = form.values.find(p.name); f != form.values.end()) value = f->second;
        value = Trim(value);
        const std::string label = Tr(FieldLabel(ctl, p.name));
        if (value.empty()) {
            if (p.type == "number_or_reset") value = "reset";
            else if (p.type == "optional_integer") {
                // an optional value left empty is left out of the command
                const std::string key = " {" + p.name + "}";
                if (const size_t at = out.find(key); at != std::string::npos) { out.erase(at, key.size()); continue; }
            } else {
                *error = Fmt("{0}: please fill out this field.", {label});
                return false;
            }
        } else if (p.type != "single_token" && p.choices.empty() && p.name != "reward" && p.name != "lens" && p.name != "timeline" && p.name != "difficulty" &&
                   p.name != "mode" && p.name != "category") {
            char* end = nullptr;
            const double v = std::strtod(value.c_str(), &end);
            if (!end || *end || !std::isfinite(v)) { *error = Fmt("{0}: please enter a number.", {label}); return false; }
            if (p.type.find("integer") != std::string::npos && std::floor(v) != v) { *error = Fmt("{0}: please enter a whole number.", {label}); return false; }
            if ((p.hasMin && v < p.min) || (p.hasMax && v > p.max)) {
                *error = Fmt("{0}: please enter a value from {1} to {2}.", {label, p.hasMin ? Num(p.min) : "\xE2\x80\x94", p.hasMax ? Num(p.max) : "\xE2\x80\x94"});
                return false;
            }
            value = Num(v);
        } else if (value.find_first_of(" \t\r\n") != std::string::npos && p.type == "single_token") {
            *error = Fmt("{0}: no spaces.", {label});
            return false;
        }
        const std::string key = "{" + p.name + "}";
        for (size_t at = out.find(key); at != std::string::npos; at = out.find(key, at + value.size())) out.replace(at, key.size(), value);
    }
    *line = out;
    return true;
}

}  // namespace ep::ingame::view
