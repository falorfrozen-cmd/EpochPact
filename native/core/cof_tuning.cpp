#include "cof_tuning.hpp"
#include "cof.hpp"
#include "cof_tuning_rules.hpp"
#include "managed.hpp"
#include "loot.hpp"
#include "hook.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <memory>
#include <sstream>

namespace ep::cof::tuning {
namespace {
using namespace managed;
enum Key { Charge, Reward, Enemy, Echo, Exalted, T7, LP, Celerity, Charity, Duplication, Count };
struct Setting {
    const char* name;
    std::atomic<double> value;
    std::atomic<uint64_t> calls{0}, applied{0}, refused{0};
    std::atomic<double> lastBase{0}, lastEffective{0};
};
Setting settings[Count]{{"charge", 1}, {"reward", 1}, {"enemy", -1}, {"echo", -1},
    {"exalted", 1}, {"t7", 1}, {"lp", 1}, {"celerity", 1}, {"charity", 1}, {"duplication", 1}};
bool ready = false;
std::atomic<uint64_t> faults{0};
size_t actorGenerator, generatorActor, exaltedExtra, t7Chance, rareExalted, enemyChance, echoChance;
size_t celerityExtra, charityExtra, duplicationChance, rewardCount, rewardBaseFavor, slotReward, slotProgress;
game::MethodRef chargeMethod, rewardMethod, affixMethod, lpMethod, echoMethod, prophecyData;
using ChargeFn = void (*)(void*, int, void*, const il2cpp::Method*);
using RewardFn = void (*)(void*, void*, uint64_t, const il2cpp::Method*); // Nullable<LensType> is 8 bytes by value.
using AffixFn = void (*)(void*, void**, int, bool, int, void*, bool, void*, void*, bool, const il2cpp::Method*);
using LPFn = float (*)(void*, void*, void*, const il2cpp::Method*);
using EchoFn = void (*)(void*, const void*, void*, bool, int, int, int, const il2cpp::Method*);
ChargeFn originalCharge = nullptr;
RewardFn originalReward = nullptr;
AffixFn originalAffixes = nullptr;
LPFn originalLP = nullptr;
EchoFn originalEcho = nullptr;
thread_local unsigned chargeDepth = 0, rewardDepth = 0, affixDepth = 0, enemyDepth = 0, echoDepth = 0;
struct Depth { unsigned& n; explicit Depth(unsigned& v) : n(v) { ++n; } ~Depth() { --n; } };
bool Active(Key k) { const double v = settings[k].value.load(); return k == Enemy || k == Echo ? v >= 0 : v != 1; }
void Record(Key k, double base, double effective) {
    ++settings[k].applied; settings[k].lastBase = base; settings[k].lastEffective = effective;
}
bool Owner(TuningContext& c) {
    if (!game::IsOfflinePlay()) return false;
    c = CurrentForTuning(); return c.member;
}
std::vector<void*> Slots(void* slots) {
    if (!slots || Get<uintptr_t>(slots, 0x18) != 4) throw std::runtime_error("unexpected CoF slots");
    std::vector<void*> result;
    for (int i = 0; i < 4; ++i) result.push_back(Get<void*>(slots, 0x20 + i * sizeof(void*)));
    return result;
}
int Lens(void* slot) {
    const auto off = Offset("ProphecySlot", "<Lens>k__BackingField", "LE.Factions");
    return Get<bool>(slot, off) ? Get<int>(slot, off + 4) : -1;
}
// Only primitive fields borrowed for one synchronous normal method call. Root
// every object, restore on return/exception, and never persist shared asset edits.
struct Borrowed {
    struct Change { void* object; size_t offset; uint32_t previous; };
    std::vector<Change> changes;
    std::vector<std::unique_ptr<Root>> roots;
    void Pin(void* object) { roots.push_back(std::make_unique<Root>(object)); }
    void Bits(void* object, size_t offset, uint32_t value) {
        const auto previous = Get<uint32_t>(object, offset);
        changes.push_back({object, offset, previous});
        std::memcpy(static_cast<char*>(object) + offset, &value, sizeof(value));
    }
    void Float(void* object, size_t offset, float value) {
        if (!std::isfinite(value)) throw std::runtime_error("nonfinite CoF coefficient");
        Bits(object, offset, std::bit_cast<uint32_t>(value));
    }
    void Restore() {
        std::string why;
        if (!game::Guarded([&] {
            for (auto i = changes.rbegin(); i != changes.rend(); ++i)
                std::memcpy(static_cast<char*>(i->object) + i->offset, &i->previous, sizeof(i->previous));
        }, &why)) { ++faults; Log("cof tuning restore failed: %s", why.c_str()); }
        changes.clear();
    }
    ~Borrowed() { Restore(); }
};
bool Checked(const std::function<void()>& work) {
    std::string why;
    if (!game::Guarded(work, &why)) { ++faults; Log("cof tuning refused preparation: %s", why.c_str()); return false; }
    return true;
}
void LensCharge(Borrowed& fields, void* data) {
    for (const auto [key, offset] : {std::pair{Celerity, celerityExtra}, std::pair{Charity, charityExtra}}) {
        if (!Active(key)) continue;
        const float base = Get<float>(data, offset);
        if (!std::isfinite(base) || base < 0) throw std::runtime_error("invalid lens charge coefficient");
        const float effective = static_cast<float>(base * settings[key].value.load());
        fields.Float(data, offset, effective); Record(key, base, effective);
    }
}
void ChargeDetour(void* self, int amount, void* slots, const il2cpp::Method* method) {
    ++settings[Charge].calls;
    if (chargeDepth || amount <= 0 || !(Active(Charge) || Active(Celerity) || Active(Charity))) {
        originalCharge(self, amount, slots, method); return;
    }
    Depth depth(chargeDepth); Borrowed fields; int scaled = amount;
    if (!Checked([&] {
        TuningContext c{}; if (!Owner(c) || c.slots != slots) { ++settings[Charge].refused; return; }
        const auto all = Slots(c.slots);
        if (std::find(all.begin(), all.end(), self) == all.end()) { ++settings[Charge].refused; return; }
        void* reward = Get<void*>(self, slotReward);
        if (!reward) return;
        fields.Pin(c.actor); fields.Pin(self);
        void* data = Invoke(prophecyData); fields.Pin(data); LensCharge(fields, data);
        double factor = Get<float>(reward, rewardBaseFavor);
        if (Lens(self) == 0) factor *= 1.0 + Get<float>(data, celerityExtra);
        for (void* slot : all)
            if (slot && slot != self && Lens(slot) == 2) factor *= 1.0 + Get<float>(data, charityExtra);
        scaled = rules::ChargeInput(amount, settings[Charge].value.load(), factor, Get<int>(self, slotProgress));
        Record(Charge, amount, scaled);
    })) { fields.Restore(); scaled = amount; }
    originalCharge(self, scaled, slots, method);
}
void RewardDetour(void* self, void* actor, uint64_t lens, const il2cpp::Method* method) {
    ++settings[Reward].calls;
    if (rewardDepth || !(Active(Reward) || Active(Duplication))) { originalReward(self, actor, lens, method); return; }
    Depth depth(rewardDepth); Borrowed fields;
    if (!Checked([&] {
        TuningContext c{}; if (!Owner(c) || c.actor != actor) { ++settings[Reward].refused; return; }
        bool selected = false;
        for (void* slot : Slots(c.slots)) if (slot && Get<void*>(slot, slotReward) == self) selected = true;
        if (!selected) { ++settings[Reward].refused; return; }
        fields.Pin(actor); fields.Pin(self);
        if (Active(Reward)) {
            const int base = Get<int>(self, rewardCount), effective = rules::RewardItems(base, settings[Reward].value.load());
            fields.Bits(self, rewardCount, static_cast<uint32_t>(effective)); Record(Reward, base, effective);
        }
        if (Active(Duplication)) {
            void* data = Invoke(prophecyData); fields.Pin(data);
            const float base = Get<float>(data, duplicationChance);
            if (!std::isfinite(base) || base < 0 || base > 1) throw std::runtime_error("invalid lens reward chance");
            const float effective = rules::Probability(base, settings[Duplication].value.load());
            fields.Float(data, duplicationChance, effective); Record(Duplication, base, effective);
        }
    })) fields.Restore();
    originalReward(self, actor, lens, method);
}
void AffixDetour(void* self, void** item, int level, bool compatible, int context, void* faction,
    bool maxTier4, void* modifiers, void* ids, bool extra, const il2cpp::Method* method) {
    ++settings[Exalted].calls; ++settings[T7].calls;
    Borrowed fields;
    if (!affixDepth && rules::AffixContext(context) && (Active(Exalted) || Active(T7))) {
        if (!Checked([&] {
            TuningContext c{};
            if (!Owner(c) || Get<void*>(self, generatorActor) != c.actor) { ++settings[Exalted].refused; return; }
            fields.Pin(c.actor); fields.Pin(self);
            if (Active(Exalted)) {
                const float base = Get<float>(self, exaltedExtra), rare = Get<float>(self, rareExalted);
                if (!std::isfinite(base) || base < 0 || !std::isfinite(rare) || rare < 0 || rare > 1)
                    throw std::runtime_error("invalid Exalted coefficients");
                const float effective = rules::RollCoefficient(base, settings[Exalted].value.load());
                fields.Float(self, exaltedExtra, effective);
                fields.Float(self, rareExalted, rules::Probability(rare, settings[Exalted].value.load()));
                Record(Exalted, base, effective);
            }
            if (Active(T7)) {
                const float base = Get<float>(self, t7Chance);
                if (!std::isfinite(base) || base < 0) throw std::runtime_error("invalid T7 roll coefficient");
                const float effective = rules::RollCoefficient(base, settings[T7].value.load());
                fields.Float(self, t7Chance, effective); Record(T7, base, effective);
            }
        })) fields.Restore();
    }
    Depth depth(affixDepth);
    originalAffixes(self, item, level, compatible, context, faction, maxTier4, modifiers, ids, extra, method);
}
float LPDetour(void* self, void* unique, void* actor, const il2cpp::Method* method) {
    ++settings[LP].calls;
    const float base = originalLP(self, unique, actor, method);
    float result = base;
    if (Active(LP) && base > 1 && std::isfinite(base) && !Checked([&] {
        TuningContext c{}; if (!Owner(c) || c.actor != actor) { ++settings[LP].refused; return; }
        result = static_cast<float>(base * settings[LP].value.load()); Record(LP, base, result);
    })) result = base;
    return result;
}
void ChanceScope(Key key, Borrowed& fields, void* expectedActor = nullptr) {
    TuningContext c{};
    if (!Owner(c) || (expectedActor && c.actor != expectedActor)) { ++settings[key].refused; return; }
    fields.Pin(c.actor); fields.Pin(c.faction);
    const size_t offset = key == Enemy ? enemyChance : echoChance;
    const float base = Get<float>(c.faction, offset);
    if (!std::isfinite(base) || base < 0 || base > 1) throw std::runtime_error("invalid double-drop chance");
    const float effective = static_cast<float>(settings[key].value.load() / 100.0);
    fields.Float(c.faction, offset, effective); Record(key, base, effective);
}
void EchoDetour(void* actor, const void* position, void* reward, bool extra, int overrideIndex, int source, int sourceIndex, const il2cpp::Method* method) {
    ++settings[Echo].calls; Borrowed fields;
    if (source == 1 && !echoDepth && Active(Echo) && !Checked([&] { ChanceScope(Echo, fields, actor); })) fields.Restore();
    Depth depth(echoDepth); originalEcho(actor, position, reward, extra, overrideIndex, source, sourceIndex, method);
}
struct HookSpec { game::MethodRef method; void* detour; void** original; };
std::vector<HookSpec> Required() {
    std::vector<HookSpec> result;
    if (Active(Charge) || Active(Celerity) || Active(Charity)) result.push_back({chargeMethod, reinterpret_cast<void*>(&ChargeDetour), reinterpret_cast<void**>(&originalCharge)});
    if (Active(Reward) || Active(Duplication)) result.push_back({rewardMethod, reinterpret_cast<void*>(&RewardDetour), reinterpret_cast<void**>(&originalReward)});
    if (Active(Exalted) || Active(T7)) result.push_back({affixMethod, reinterpret_cast<void*>(&AffixDetour), reinterpret_cast<void**>(&originalAffixes)});
    if (Active(LP)) result.push_back({lpMethod, reinterpret_cast<void*>(&LPDetour), reinterpret_cast<void**>(&originalLP)});
    if (Active(Echo)) result.push_back({echoMethod, reinterpret_cast<void*>(&EchoDetour), reinterpret_cast<void**>(&originalEcho)});
    return result;
}
void Reconcile() {
    const auto required = Required(); std::string why; std::vector<void*> fresh;
    try {
        for (const auto& h : required) if (!hook::IsInstalled(h.method.code)) {
            if (!hook::Install(h.method.code, h.detour, h.original, &why)) throw std::runtime_error(why);
            fresh.push_back(h.method.code);
        }
        if (!loot::SetCoFDropDependency(Active(Enemy), &why)) throw std::runtime_error(why);
        for (const auto& m : {chargeMethod, rewardMethod, affixMethod, lpMethod, echoMethod})
            if (std::none_of(required.begin(), required.end(), [&](const auto& h) { return h.method.code == m.code; }) &&
                hook::IsInstalled(m.code) && !hook::Remove(m.code, &why)) throw std::runtime_error(why);
    } catch (...) {
        for (void* address : fresh) hook::Remove(address, nullptr);
        throw;
    }
}
game::MethodRef Resolve(const char* cls, const char* name, int arity, const char* ns = "LE.Factions") {
    const auto result = game::FindMethod("LE.dll", ns, cls, name, arity);
    if (!result) throw std::runtime_error(std::string("missing CoF tuning method: ") + cls + "." + name);
    return result;
}
}
bool Init() {
    std::string why;
    ready = game::Guarded([&] {
        chargeMethod = Resolve("ProphecySlot", "AddFavor", 2); rewardMethod = Resolve("ProphecySlotReward", "SpawnRewardForPlayer", 2);
        affixMethod = Resolve("GenerateItems", "GenerateAffixes", 9, ""); lpMethod = Resolve("ItemData", "GetUniqueLPRollMultiplierFromCoF", 2, "");
        echoMethod = Resolve("MonolithItemManager", "SpawnEchoSpecificRewards", 7, "LE.Gameplay.Monolith");
        prophecyData = Resolve("ProphecyData", "get_Data", 0);
        actorGenerator = Offset("Actor", "generateItems"); generatorActor = Offset("GenerateItems", "actor");
        exaltedExtra = Offset("GenerateItems", "<ChanceForExaltedAffixesMultiplier>k__BackingField");
        t7Chance = Offset("GenerateItems", "<ChanceForT7Affixes>k__BackingField");
        rareExalted = Offset("GenerateItems", "<ChanceForRareItemsToBecomeExalted0To1>k__BackingField");
        enemyChance = Offset("CircleOfFortune", "<ChanceForDoubleEnemyRewards0To1>k__BackingField", "LE.Factions");
        echoChance = Offset("CircleOfFortune", "<ChanceForDoubleEchoRewards0To1>k__BackingField", "LE.Factions");
        celerityExtra = Offset("ProphecyData", "celerityLensExtraCharge", "LE.Factions");
        charityExtra = Offset("ProphecyData", "charityLensExtraCharge", "LE.Factions");
        duplicationChance = Offset("ProphecyData", "duplicationLensChance", "LE.Factions");
        rewardCount = Offset("ProphecySlotReward", "itemsDropped", "LE.Factions");
        rewardBaseFavor = Offset("ProphecySlotReward", "baseFavorMultiplier", "LE.Factions");
        slotReward = Offset("ProphecySlot", "<Reward>k__BackingField", "LE.Factions");
        slotProgress = Offset("ProphecySlot", "<ChargeInProgress>k__BackingField", "LE.Factions");
    }, &why);
    Log("cof tuning: %s%s", ready ? "metadata ready" : "unavailable: ", ready ? "" : why.c_str()); return ready;
}
std::string Set(const std::string& name, double value) {
    return Run([=] {
        if (!ready) throw std::runtime_error("CoF tuning metadata unavailable");
        Key key = Count;
        for (int i = 0; i < Count; ++i) if (name == settings[i].name) key = static_cast<Key>(i);
        if (key == Count) throw std::runtime_error("unknown CoF tuning control");
        const bool probability = key == Enemy || key == Echo;
        if (!(probability ? value == -1 || rules::Chance(value) : key == Reward ? rules::RewardMultiplier(value) : cof::rules::Multiplier(value)))
            throw std::runtime_error("invalid CoF tuning value: multiplier 1..100, reward 1..25, chance 0..100/reset");
        const bool enabling = probability ? value >= 0 : value != 1;
        if (enabling) { TuningContext c{}; if (!Owner(c)) throw std::runtime_error("join offline Circle of Fortune first"); }
        const double previous = settings[key].value.exchange(value);
        try { Reconcile(); }
        catch (...) { settings[key].value = previous; try { Reconcile(); } catch (...) { Log("cof tuning rollback reconciliation failed"); } throw; }
        return "{\"ok\":true,\"setting\":" + Json(name) + ",\"value\":" + (value == -1 ? "null" : std::to_string(value)) + "}";
    });
}
std::string Read() {
    if (!ready) return "{\"ready\":false}";
    const auto c = CurrentForTuning(); Root data(Invoke(prophecyData));
    void* gen = Get<void*>(c.actor, actorGenerator);
    std::ostringstream out; out << "{\"ready\":" << Boolean(ready) << ",\"settings\":{";
    for (int i = 0; i < Count; ++i) {
        if (i) out << ','; const auto v = settings[i].value.load();
        out << Json(settings[i].name) << ':' << (v == -1 ? "null" : std::to_string(v));
    }
    out << "},\"base\":{\"enemy\":" << Get<float>(c.faction, enemyChance) << ",\"echo\":" << Get<float>(c.faction, echoChance)
        << ",\"exalted\":" << Get<float>(gen, exaltedExtra) << ",\"t7\":" << Get<float>(gen, t7Chance)
        << ",\"rareToExalted\":" << Get<float>(gen, rareExalted)
        << ",\"celerity\":" << Get<float>(data.Get(), celerityExtra) << ",\"charity\":" << Get<float>(data.Get(), charityExtra)
        << ",\"duplication\":" << Get<float>(data.Get(), duplicationChance) << "},\"telemetry\":{";
    for (int i = 0; i < Count; ++i) {
        if (i) out << ','; const auto& s = settings[i];
        out << Json(s.name) << ":{\"calls\":" << s.calls.load() << ",\"applied\":" << s.applied.load()
            << ",\"refused\":" << s.refused.load() << ",\"lastBase\":" << s.lastBase.load() << ",\"lastEffective\":" << s.lastEffective.load() << '}';
    }
    const auto drop = game::FindMethod("LE.dll", "", "ItemDrop", "DropItem", 18);
    out << "},\"hooks\":{\"charge\":" << Boolean(hook::IsInstalled(chargeMethod.code))
        << ",\"reward\":" << Boolean(hook::IsInstalled(rewardMethod.code))
        << ",\"affixes\":" << Boolean(hook::IsInstalled(affixMethod.code))
        << ",\"lp\":" << Boolean(hook::IsInstalled(lpMethod.code))
        << ",\"echo\":" << Boolean(hook::IsInstalled(echoMethod.code))
        << ",\"dropDispatcher\":" << Boolean(drop && hook::IsInstalled(drop.code))
        << "},\"faults\":" << faults.load() << ",\"maxItemsPerReward\":" << rules::MaxItemsPerReward << '}'; return out.str();
}
std::string Status() {
    std::ostringstream out; out << "CoF tuning:";
    for (const auto& s : settings) out << ' ' << s.name << '=' << (s.value.load() == -1 ? "default" : std::to_string(s.value.load()));
    out << "; faults " << faults.load(); return out.str();
}
void WithEnemyLoot(bool enemyDeath, const std::function<void()>& original) {
    ++settings[Enemy].calls; Borrowed fields;
    if (enemyDeath && !enemyDepth && Active(Enemy) && !Checked([&] { ChanceScope(Enemy, fields); })) fields.Restore();
    Depth depth(enemyDepth); original();
}
}
