#include "ailments.hpp"
#include "ailment_rules.hpp"
#include "managed.hpp"
#include "common.hpp"
#include "lifecycle.hpp"
#include "hook.hpp"
#include <unordered_map>
#include <atomic>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

// Every ailment of the game is an Ailment asset in AilmentList.list: maxInstances is its hard stack cap
// (Time Rot 12), duration its base duration in seconds. Every application goes through
// AilmentReceiver.ApplyAilmentWithDamageStats, which reads both: the stack cap to drop the oldest stack, the duration
// for the new stack's totalDuration ((1 + increased duration) x duration, kept on the stack). For an application by the
// player's side (its Alignment: the character, minions, totems) the hook writes the multiplied values for that call
// only and puts the game's own back after, so enemies' ailments keep theirs. Nothing is saved.
namespace ep::ailments {
namespace {
using namespace managed;
enum Key { Stacks, Duration, Count };
struct Setting { const char* name; rules::Range range; std::atomic<double> value; };
Setting settings[Count]{{"stacks", rules::kStacks, 1}, {"duration", rules::kDuration, 1}};
struct Original { std::unique_ptr<Root> object; void* address = nullptr; int id = 0, maxInstances = 0; float duration = 0; std::string name; bool positive = false; };
std::vector<Original> originals;
game::MethodRef listGet;
size_t oAlignmentManager = 0, oAlignment = 0;
size_t oList = 0, oId = 0, oMax = 0, oDuration = 0, oName = 0, oPositive = 0;
bool ready = false, applied = false;
uint64_t nextCheck = 0, faults = 0;
std::string lastError;

bool Active() { for (auto& s : settings) if (s.value.load() != s.range.neutral) return true; return false; }
std::atomic<void*> playerAlignment{nullptr};  // the player's side, looked up twice a second
std::unique_ptr<Root> playerAlignmentRoot;
struct Own { int maxInstances; float duration; };
std::unordered_map<void*, Own> own;  // the game's values per Ailment asset, as first seen outside a hooked call
uint64_t scaledCalls = 0, otherCalls = 0;

// The game's ailments now, each with its first-seen values.
void Capture() {
    void* list = Invoke(listGet);
    if (!list) throw std::runtime_error("the game's ailment list is not loaded");
    void* items = Get<void*>(list, oList);
    if (!items) throw std::runtime_error("the game's ailment list is empty");
    void* array = Get<void*>(items, 0x10);
    const int size = Get<int>(items, 0x18);
    for (int i = 0; array && i < size; ++i) {
        void* a = Get<void*>(array, 0x20 + 8 * static_cast<size_t>(i));
        if (!a || !game::IsAlive(a)) continue;
        bool known = false;
        for (auto& o : originals) if (o.address == a) { known = true; break; }
        if (known) continue;
        Original o;
        o.object = std::make_unique<Root>(a); o.address = a;
        o.id = Get<int>(a, oId); o.maxInstances = Get<int>(a, oMax); o.duration = Get<float>(a, oDuration);
        o.name = Text(Get<void*>(a, oName)); o.positive = Get<bool>(a, oPositive);
        originals.push_back(std::move(o));
    }
}
void RefreshSide() {
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    void* manager = actor && game::IsAlive(actor) ? Get<void*>(actor, Offset("Actor", "alignmentManager")) : nullptr;
    void* alignment = manager ? Get<void*>(manager, Offset("AlignmentManager", "alignment")) : nullptr;
    if (alignment != playerAlignment.load()) { playerAlignmentRoot = alignment ? std::make_unique<Root>(alignment) : nullptr; playerAlignment = alignment; }
}
using ApplyFn = void* (*)(void* self, void* ailment, void* creator, void* ability, void* originalAbility, void* primaryAbility, void* damageStats, void* alignment,
                          int useType, float stacks, float increasedDuration, float increasedEffect, float increasedHealing, bool appliedBySpreading, float elapsedDuration,
                          const il2cpp::Method* method);
ApplyFn originalApply = nullptr;
// The player's side's multiplied values in the asset while f runs, the values that were there put back after.
template<class F> auto Scaled(void* ailment, void* creator, void* alignment, F&& f) {
    const double s = settings[Stacks].value.load(), d = settings[Duration].value.load();
    void* side = playerAlignment.load();
    // a self-inflicted buff comes without an alignment: then the creator's own says whose it is
    void* whose = alignment;
    if (!whose && creator && side) { void* manager = Get<void*>(creator, oAlignmentManager); whose = manager ? Get<void*>(manager, oAlignment) : nullptr; }
    if (!ailment || !side || whose != side || (s == 1 && d == 1) || lifecycle::Stopping()) {
        if (ailment && side && whose != side) ++otherCalls;
        return f();
    }
    char* a = static_cast<char*>(ailment);
    int& max = *reinterpret_cast<int*>(a + oMax);
    float& duration = *reinterpret_cast<float*>(a + oDuration);
    const int nowMax = max; const float nowDuration = duration;  // what is there now: the game's, or an outer call's
    auto it = own.find(ailment);
    if (it == own.end()) it = own.emplace(ailment, Own{nowMax, nowDuration}).first;
    max = rules::ScaleStacks(it->second.maxInstances, s);
    duration = rules::ScaleDuration(it->second.duration, d);
    ++scaledCalls;
    struct Back { int& max; float& duration; int m; float d; ~Back() { max = m; duration = d; } } back{max, duration, nowMax, nowDuration};
    return f();
}
void* ApplyHook(void* self, void* ailment, void* creator, void* ability, void* originalAbility, void* primaryAbility, void* damageStats, void* alignment,
                int useType, float stacks, float increasedDuration, float increasedEffect, float increasedHealing, bool appliedBySpreading, float elapsedDuration,
                const il2cpp::Method* method) {
    return Scaled(ailment, creator, alignment, [&] {
        return originalApply(self, ailment, creator, ability, originalAbility, primaryAbility, damageStats, alignment, useType, stacks, increasedDuration, increasedEffect,
                             increasedHealing, appliedBySpreading, elapsedDuration, method);
    });
}
// The outer call: it limits the stacks asked for to the cap before the inner one.
using OuterFn = void (*)(void* self, void* ailment, void* creator, void* ability, void* originalAbility, void* primaryAbility, void* increases, void* alignment,
                         float stacks, float increasedDuration, float increasedEffect, int useType, bool respectConversions, float moreDamage, float elapsedDuration,
                         const il2cpp::Method* method);
OuterFn originalOuter = nullptr;
void OuterHook(void* self, void* ailment, void* creator, void* ability, void* originalAbility, void* primaryAbility, void* increases, void* alignment,
               float stacks, float increasedDuration, float increasedEffect, int useType, bool respectConversions, float moreDamage, float elapsedDuration,
               const il2cpp::Method* method) {
    Scaled(ailment, creator, alignment, [&] {
        originalOuter(self, ailment, creator, ability, originalAbility, primaryAbility, increases, alignment, stacks, increasedDuration, increasedEffect, useType,
                      respectConversions, moreDamage, elapsedDuration, method);
        return 0;
    });
}
void Apply() { Capture(); RefreshSide(); applied = Active(); }
void Restore() { playerAlignment = nullptr; playerAlignmentRoot.reset(); applied = false; }  // online: the hook leaves every call alone

std::string State() {
    std::ostringstream out; out.setf(std::ios::fixed); out.precision(3);
    out << "{\"ok\":true,\"available\":" << Boolean(ready) << ",\"offline\":" << Boolean(game::IsOfflinePlay()) << ",\"settings\":{";
    for (int k = 0; k < Count; ++k) out << (k ? "," : "") << '"' << settings[k].name << "\":{\"value\":" << settings[k].value.load() << ",\"available\":" << Boolean(ready) << '}';
    out << "},\"applied\":" << Boolean(applied) << ",\"side\":" << Boolean(playerAlignment.load() != nullptr) << ",\"scaledCalls\":" << scaledCalls << ",\"otherCalls\":" << otherCalls
        << ",\"faults\":" << faults << ",\"error\":" << Json(lastError);
    int capped = 0, timed = 0;
    for (auto& o : originals) { capped += rules::HasCap(o.maxInstances); timed += o.duration > 0; }
    out << ",\"ailments\":" << originals.size() << ",\"capped\":" << capped << ",\"timed\":" << timed << ",\"list\":[";
    bool first = true;
    for (auto& o : originals) {
        void* a = o.object ? o.object->Get() : nullptr;
        if (!a || !game::IsAlive(a) || (!rules::HasCap(o.maxInstances) && o.duration <= 0)) continue;
        out << (first ? "" : ",") << "{\"id\":" << o.id << ",\"name\":" << Json(o.name) << ",\"positive\":" << Boolean(o.positive) << ",\"max\":" << o.maxInstances
            << ",\"maxNow\":" << rules::ScaleStacks(o.maxInstances, settings[Stacks].value) << ",\"maxGame\":" << Get<int>(a, oMax) << ",\"duration\":" << o.duration
            << ",\"durationNow\":" << rules::ScaleDuration(o.duration, settings[Duration].value) << ",\"durationGame\":" << Get<float>(a, oDuration) << '}';
        first = false;
    }
    out << "]}";
    return out.str();
}
}  // namespace

bool Init() {
    try {
        listGet = game::FindMethod("LE.dll", "", "AilmentList", "get", 0);
        oAlignmentManager = Offset("Actor", "alignmentManager"); oAlignment = Offset("AlignmentManager", "alignment");
        oList = Offset("AilmentList", "list"); oId = Offset("Ailment", "id"); oMax = Offset("Ailment", "maxInstances"); oDuration = Offset("Ailment", "duration");
        oName = Offset("Ailment", "displayName"); oPositive = Offset("Ailment", "positive");
        const auto apply = game::FindMethod("LE.dll", "", "AilmentReceiver", "ApplyAilmentWithDamageStats", 14);
        std::string why;
        if (!apply || !hook::Install(apply.code, reinterpret_cast<void*>(&ApplyHook), reinterpret_cast<void**>(&originalApply), &why)) throw std::runtime_error("apply hook: " + why);
        const auto outer = game::FindMethod("LE.dll", "", "AilmentReceiver", "ApplyAilment", 14);
        if (!outer || !hook::Install(outer.code, reinterpret_cast<void*>(&OuterHook), reinterpret_cast<void**>(&originalOuter), &why)) throw std::runtime_error("outer apply hook: " + why);
        ready = static_cast<bool>(listGet);
    } catch (const std::exception& e) { ready = false; lastError = e.what(); }
    Log("ailments: stack and duration settings %s%s%s", ready ? "ready" : "unavailable", lastError.empty() ? "" : ": ", lastError.c_str());
    return ready;
}
void Frame() {
    if (!ready || lifecycle::Stopping()) return;
    const auto now = GetTickCount64();
    if (now < nextCheck) return;
    nextCheck = now + 500;
    if (!Active() && !applied) return;  // nothing changed: no game calls at all
    std::string why;
    if (!game::Guarded([] { if (game::IsOfflinePlay()) Apply(); else Restore(); }, &why)) {
        ++faults; lastError = why; nextCheck = now + 10000;
        Log("ailments: %s", why.c_str());
    }
}
std::string Read() {
    if (!ready) return State();
    return Run([] { if (game::IsOfflinePlay()) Capture(); return State(); });
}
std::string Set(const std::string& key, double value) {
    int k = 0;
    while (k < Count && key != settings[k].name) ++k;
    if (k == Count) return "{\"ok\":false,\"error\":\"use ailmentset stacks|duration value\"}";
    if (!rules::InRange(settings[k].range, value)) {
        std::ostringstream e; e << "{\"ok\":false,\"error\":\"" << settings[k].name << " must be " << settings[k].range.low << " to " << settings[k].range.high << "\"}";
        return e.str();
    }
    if (!ready) return "{\"ok\":false,\"error\":\"Ailment data is unavailable in this game build.\"}";
    return Run([k, value] {
        const double previous = settings[k].value.exchange(value);
        try { if (game::IsOfflinePlay()) Apply(); else if (applied) Restore(); }
        catch (...) { settings[k].value = previous; throw; }
        lastError.clear();
        return State();
    });
}
void ReleaseRoots() { playerAlignment = nullptr; playerAlignmentRoot.reset(); originals.clear(); own.clear(); applied = false; }

#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
namespace {
const il2cpp::Method* Overload(const char* cls, const char* name, const char* parameter) {
    auto& a = il2cpp::api(); void* iterator = nullptr;
    while (auto* m = a.class_get_methods(game::FindClass("LE.dll", "", cls), &iterator)) {
        if (std::string(a.method_get_name(m)) != name || a.method_get_param_count(m) != 1) continue;
        char* type = a.type_get_name(a.method_get_param(m, 0));
        const bool match = type && std::string(type) == parameter;
        if (type) a.free(type);
        if (match) return m;
    }
    throw std::runtime_error(std::string("missing ") + cls + "." + name);
}
}  // namespace
// Test builds: the loaded character inflicts <stacks> stacks of ailment <id> on itself; how many it has after.
std::string Test(int id, int stacks, bool foe) {
    return Run([=]() -> std::string {
        if (!game::IsOfflinePlay()) throw std::runtime_error("offline only");
        if (stacks < 1 || stacks > 1000) throw std::runtime_error("stacks 1..1000");
        void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
        if (!actor || !game::IsAlive(actor)) throw std::runtime_error("no character");
        void* receiver = Get<void*>(actor, Offset("Actor", "ailmentReceiver"));
        int aid = id; void* lookup[]{&aid};
        Root ailment(Invoke(Overload("AilmentList", "getAilment", "System.Int32"), nullptr, lookup));
        if (!ailment.Get()) throw std::runtime_error("no such ailment");
        if (!foe) {
            int count = stacks; float increased = 0; bool conversions = false;
            void* args[]{ailment.Get(), nullptr, &count, &increased, actor, &conversions};
            Invoke(Method(receiver, "SelfInflictAilment", 6), receiver, args);
        } else {
            // as an enemy would: the same call with a foe's alignment
            void* manager = Get<void*>(actor, Offset("Actor", "alignmentManager"));
            void* mine = Get<void*>(manager, Offset("AlignmentManager", "alignment"));
            const auto foes = Entries(Get<void*>(mine, Offset("Alignment", "foes")));
            if (foes.empty() || !foes.front()) throw std::runtime_error("no foe alignment");
            void* stats = Get<void*>(actor, Offset("Actor", "stats"));
            float count = static_cast<float>(stacks), none = 0; int useType = 9; bool conversions = false;
            void* args[]{ailment.Get(), actor, nullptr, nullptr, nullptr, stats, foes.front(), &count, &none, &none, &useType, &conversions, &none, &none};
            Invoke(game::FindMethod("LE.dll", "", "AilmentReceiver", "ApplyAilment", 14), receiver, args);
        }
        void* sa[]{ailment.Get()};
        const float now = Value<float>(Overload("AilmentReceiver", "getStacks", "Ailment"), receiver, sa);  // the game counts stacks as a float
        std::ostringstream out;
        out << "{\"ok\":true,\"ailment\":" << Json(Text(Get<void*>(ailment.Get(), oName))) << ",\"stacks\":" << now << ",\"max\":" << Get<int>(ailment.Get(), oMax)
            << ",\"duration\":" << Get<float>(ailment.Get(), oDuration) << "}";
        return out.str();
    });
}
#endif
}  // namespace ep::ailments
