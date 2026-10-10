#include "cof.hpp"
#include "cof_tuning.hpp"
#include "managed.hpp"
#include "cof_rules.hpp"
#include "progression.hpp"
#include "mutation_transaction.hpp"
#include <memory>
#include "hook.hpp"
#include <atomic>
#include <limits>
#include <sstream>

namespace ep::cof {
namespace {
using namespace managed;
game::MethodRef gain, gainReputation;
bool ready = false;
using GainFn = void (*)(void*, int, bool, bool, const il2cpp::Method*);
GainFn original = nullptr;
using ReputationFn = void (*)(void*, int, const il2cpp::Method*);
ReputationFn originalReputation = nullptr;
std::atomic<double> multiplier{1};
std::atomic<double> reputationMultiplier{1};
std::atomic<uint64_t> gains{0}, boosted{0}, refused{0}, faults{0};
std::atomic<int> lastIn{0}, lastOut{0};
std::atomic<uint64_t> repGains{0}, repBoosted{0}, repRefused{0}, repFaults{0}, repAtCap{0}, repManual{0};
std::atomic<int> repLastIn{0}, repLastOut{0};
thread_local unsigned unscaledReputationDepth = 0;
struct UnscaledReputation {
    UnscaledReputation() { ++unscaledReputationDepth; }
    ~UnscaledReputation() { --unscaledReputationDepth; }
};
game::MethodRef Resolve(const char* cls, const char* name, int arity, const char* ns = "LE.Factions") {
    const auto m = game::FindMethod("LE.dll", ns, cls, name, arity);
    if (!m) throw std::runtime_error(std::string("missing method: ") + cls + "." + name);
    return m;
}
size_t F(const char* cls, const char* field) { return Offset(cls, field, "LE.Factions"); }
int Number(void* object, const char* field) { return Get<int>(object, F("Faction", field)); }
bool Member(void* object) { return Get<bool>(object, F("Faction", "<IsMember>k__BackingField")); }
std::vector<void*> Array(void* object, size_t max) {
    if (!object) throw std::runtime_error("required array unavailable");
    const auto n = Get<uintptr_t>(object, 0x18);
    if (n > max) throw std::runtime_error("array exceeds expected bounds");
    std::vector<void*> result;
    for (uintptr_t i = 0; i < n; ++i) result.push_back(Get<void*>(object, 0x20 + i * sizeof(void*)));
    return result;
}
struct Context { void* actor; void* provider; void* faction; void* data; std::string id, name; };
void* GetFaction(void* provider, uint8_t id) {
    void* result = nullptr; void* args[]{&id, &result};
    if (!Value<bool>(Method(provider, "TryGetFaction", 2), provider, args) || !result)
        throw std::runtime_error("faction data not loaded");
    return result;
}
Context Current(const std::string& expected = {}, bool requireMember = false) {
    if (!ready) throw std::runtime_error("CoF metadata unavailable");
    if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
    RequireSession(4);
    void* actor = Invoke(Resolve("PlayerFinder", "getPlayerActor", 0, ""));
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("enter an offline character's zone first");
    void* tracker = Get<void*>(actor, Offset("Actor", "characterDataTracker"));
    void* character = Get<void*>(tracker, Offset("CharacterDataTracker", "charData"));
    if (!Value<bool>(Resolve("CharacterData", "get_IsOffline", 0, "LE.Data"), character))
        throw std::runtime_error("loaded character is not offline");
    const auto id = Text(Invoke(Method(character, "get_Id", 0), character));
    if (!expected.empty() && (expected.find_first_not_of("0123456789") != std::string::npos || id != expected))
        throw std::runtime_error("loaded offline save id changed; refresh the panel");
    void* provider = Get<void*>(actor, Offset("Actor", "factionInfo"));
    if (!provider || Get<void*>(provider, F("FactionTracker", "actor")) != actor)
        throw std::runtime_error("faction tracker does not belong to this actor");
    void* faction = GetFaction(provider, 0);
    if (Get<void*>(faction, F("Faction", "<CachedPlayerActor>k__BackingField")) != actor)
        throw std::runtime_error("CoF does not belong to this actor");
    if (requireMember && !Member(faction)) throw std::runtime_error("join Circle of Fortune first");
    return {actor, provider, faction, Get<void*>(faction, F("Faction", "<Data>k__BackingField")), id,
        Text(Get<void*>(character, Offset("CharacterData", "<CharacterName>k__BackingField", "LE.Data")))};
}
Context Edit(const std::string& id, bool member = true) {
    if (id.empty()) throw std::runtime_error("offline save id required");
    return Current(id, member);
}
int RankOf(const Context& c) { return Number(c.faction, "<Rank>k__BackingField"); }
void* SlotArray(const Context& c) { return Get<void*>(c.faction, F("CircleOfFortune", "prophecySlots")); }
void* Slot(const Context& c, int index) {
    const auto slots = Array(SlotArray(c), 4);
    if (index < 0 || index >= static_cast<int>(slots.size()) || !slots[index])
        throw std::runtime_error("unknown or rank-locked prophecy slot");
    return slots[index];
}
std::string State(const Context& c) {
    return "{\"member\":" + std::string(Boolean(Member(c.faction))) + ",\"rank\":" + std::to_string(RankOf(c)) +
        ",\"favor\":" + std::to_string(Number(c.faction, "<Favor>k__BackingField")) +
        ",\"reputation\":" + std::to_string(Number(c.faction, "<Reputation>k__BackingField")) + "}";
}
void Save(const Context& c) {
    bool sync = false; void* args[]{&sync}; Invoke(Method(c.faction, "SaveAndSync", 1), c.faction, args);
    progression::SaveCurrent(c.id);
}
std::string Slots(const Context& c);
template<class Work> std::string Mutation(const std::string& id, const char* action, Work work) {
    Context before{};
    progression::SnapshotData snapshot;
    std::string factionStamp;
    std::vector<std::unique_ptr<Root>> roots;
    auto sync = [](const Context& c) { void* args[]{c.faction}; Invoke(Method(c.provider, "SaveFaction", 1), c.provider, args); };
    return transaction::Execute(Run, [&] {
        const auto c = Edit(id, false);
        if (id.empty()) throw std::runtime_error("offline save id required");
        const auto early = work(c, false);
        if (!early.empty()) return early;
        before = c;
        factionStamp = State(c) + Slots(c);
        for (void* object : {c.actor, c.provider, c.faction, c.data}) roots.push_back(std::make_unique<Root>(object));
        sync(c);
        snapshot = progression::CaptureSnapshotCurrent(id, action);
        return std::string();
    }, [&] { return progression::WriteCapturedSnapshot(snapshot); }, [&] {
        const auto c = Edit(id, false);
        if (c.actor != before.actor ||
                c.provider != before.provider ||
                c.faction != before.faction ||
                c.data != before.data || c.name != before.name)
            throw std::runtime_error("character or game context changed during backup; action cancelled");
        // Compare the state this operation edits. SaveFaction itself updates
        // serialized tracking data, so comparing the entire character JSON
        // would reject valid requests even when the faction has not changed.
        if (State(c) + Slots(c) != factionStamp)
            throw std::runtime_error("faction state changed during backup; refresh and try again");
        const auto details = work(c, true);
        Save(c);
        return "{\"ok\":true,\"cof\":" + State(c) + details + "}";
    }, Json);
}
void* ProphecyData() { return Invoke(Resolve("ProphecyData", "get_Data", 0)); }
void* Reward(int id) {
    if (id < 0) return nullptr;
    if (id > 65535) throw std::runtime_error("unknown prophecy reward id");
    Root data(ProphecyData()); uint16_t rid = static_cast<uint16_t>(id); void* reward = nullptr;
    void* args[]{&rid, &reward};
    if (!Value<bool>(Resolve("ProphecyData", "TryGetRewardById", 2), data.Get(), args) || !reward)
        throw std::runtime_error("unknown prophecy reward id");
    return reward;
}
int RewardID(void* reward) { return reward ? Get<uint16_t>(reward, F("ProphecySlotReward", "ID")) : -1; }
int LensID(void* slot) {
    const auto off = F("ProphecySlot", "<Lens>k__BackingField");
    return Get<bool>(slot, off) ? Get<int>(slot, off + 4) : -1;
}
const il2cpp::Method* ArrayMethod(void* array, const char* name, const std::vector<std::string>& types) {
    for (auto* cls = il2cpp::api().object_get_class(array); cls; cls = il2cpp::api().class_get_parent(cls)) {
        void* iter = nullptr;
        while (const auto* m = il2cpp::api().class_get_methods(cls, &iter)) {
            if (std::string(il2cpp::api().method_get_name(m)) != name || il2cpp::api().method_get_param_count(m) != types.size()) continue;
            bool match = true;
            for (uint32_t i = 0; i < types.size(); ++i) {
                char* type = il2cpp::api().type_get_name(il2cpp::api().method_get_param(m, i));
                if (!type || types[i] != type) match = false;
                if (type) il2cpp::api().free(type);
            }
            if (match) return m;
        }
    }
    throw std::runtime_error(std::string("missing typed array method: ") + name);
}
// Use a cloned reference array and normal slot setters. The game constructs
// its ValueTuple configuration; no boxed generic value/reference fields are written.
void* Config(const Context& c, int index, int rewardId, int lens) {
    void* originalSlot = Slot(c, index);
    if (rewardId < -1 || lens < -1 || lens > 11) throw std::runtime_error("invalid reward or lens id");
    void* slots = SlotArray(c);
    Log("cof: prepare configuration slot %d reward %d lens %d", index, rewardId, lens);
    Root copies(Invoke(ArrayMethod(slots, "Clone", {}), slots));
    Root copy(Invoke(Method(originalSlot, "Copy", 0), originalSlot));
    struct NullableLens { bool hasValue; unsigned char padding[3]{}; int value; } nullable{lens >= 0, {}, lens};
    static_assert(sizeof(NullableLens) == 8);
    void* lensArgs[]{&nullable}; Invoke(Method(copy.Get(), "SetLens", 1), copy.Get(), lensArgs);
    void* rewardArgs[]{Reward(rewardId)}; Invoke(Method(copy.Get(), "SetReward", 1), copy.Get(), rewardArgs);
    Log("cof: copied slot setters complete");
    void* setArgs[]{copy.Get(), &index};
    Invoke(ArrayMethod(copies.Get(), "SetValue", {"System.Object", "System.Int32"}), copies.Get(), setArgs);
    Log("cof: cloned slot array updated");
    void* configArgs[]{copies.Get()};
    Root config(Invoke(Resolve("CircleOfFortune", "SlotsToClientConfig", 1), nullptr, configArgs));
    if (Get<uintptr_t>(config.Get(), 0x18) != 4) throw std::runtime_error("unexpected prophecy config size");
    void* validArgs[]{config.Get()};
    if (!Value<bool>(Method(c.faction, "IsConfigValid", 1), c.faction, validArgs))
        throw std::runtime_error("configuration rejected: rank, purchased lens or duplicate reward/lens");
    return config.Get(); // Caller immediately roots it before invoking any managed code.
}
std::string Slots(const Context& c) {
    const auto slots = Array(SlotArray(c), 4);
    std::ostringstream out; out << '[';
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (i) out << ','; void* slot = slots[i]; void* args[]{&i};
        out << "{\"index\":" << i << ",\"rankRequired\":" << Value<int>(Resolve("CircleOfFortune", "GetRankToUnlockProphecySlot", 1), nullptr, args)
            << ",\"locked\":" << Boolean(!slot);
        if (slot) {
            void* reward = Get<void*>(slot, F("ProphecySlot", "<Reward>k__BackingField"));
            const int rid = RewardID(reward), lens = LensID(slot);
            out << ",\"rewardId\":" << (rid < 0 ? "null" : std::to_string(rid))
                << ",\"lens\":" << (lens < 0 ? "null" : std::to_string(lens))
                << ",\"charges\":" << static_cast<int>(Get<uint8_t>(slot, F("ProphecySlot", "<CompletedCharges>k__BackingField")))
                << ",\"chargeProgress\":" << Get<int>(slot, F("ProphecySlot", "<ChargeInProgress>k__BackingField"))
                << ",\"chargePercentage\":" << Value<float>(Method(slot, "GetChargePercentage", 0), slot)
                << ",\"baseFavorPerCharge\":" << (reward ? Value<int>(Method(reward, "get_FavorPerCharge", 0), reward) : 0);
            void* target = Get<void*>(slot, F("ProphecySlot", "<Target>k__BackingField"));
            if (target) { void* targetArgs[]{target}; out << ",\"target\":" << Json(Text(Invoke(Resolve("ProphecyData", "GetLocalizedTargetName", 1), nullptr, targetArgs))); }
        }
        out << '}';
    }
    out << ']'; return out.str();
}
void Detour(void* self, int amount, bool ignoreRep, bool ignoreMultiplier, const il2cpp::Method* method) {
    ++gains; int scaled = amount; const auto m = multiplier.load();
    if (amount > 0 && !ignoreMultiplier && m != 1) {
        bool owner = false; std::string why;
        const bool checked = game::Guarded([&] { const auto c = Current(); owner = c.faction == self && Member(self); }, &why);
        if (!checked) ++faults;
        if (owner) { scaled = rules::ScaleGain(amount, m); ++boosted; } else ++refused;
    }
    lastIn = amount; lastOut = scaled;
    original(self, scaled, ignoreRep, ignoreMultiplier, method);
}
void ReputationDetour(void* self, int amount, const il2cpp::Method* method) {
    ++repGains; int scaled = amount; const auto m = reputationMultiplier.load();
    if (unscaledReputationDepth) ++repManual;
    else if (amount > 0 && m != 1) {
        bool owner = false, capped = false; int current = 0; std::string why;
        const bool checked = game::Guarded([&] {
            const auto c = Current(); owner = c.faction == self && Member(c.faction);
            if (!owner) return;
            capped = RankOf(c) >= Value<int>(Method(c.data, "get_MaxRank", 0), c.data);
            current = Number(c.faction, "<Reputation>k__BackingField");
            if (current < 0) throw std::runtime_error("negative reputation progress");
        }, &why);
        if (!checked) ++repFaults;
        if (checked && owner) {
            if (capped) ++repAtCap;
            else { scaled = rules::ScaleReputationGain(amount, m, current); ++repBoosted; }
        } else ++repRefused;
    }
    repLastIn = amount; repLastOut = scaled;
    originalReputation(self, scaled, method);
}
}
bool Init() {
    std::string why;
    ready = game::Guarded([&] {
        gain = Resolve("Faction", "GainFavor", 3);
        gainReputation = Resolve("Faction", "GainReputation", 1);
        (void)Resolve("CircleOfFortune", "SlotsToClientConfig", 1);
        (void)F("Faction", "<CachedPlayerActor>k__BackingField");
        const auto* field = game::FindStaticField("LE.dll", "LE.Factions", "Faction", "MaxFavor");
        int max = 0;
        if (!field) throw std::runtime_error("max favor constant unavailable");
        il2cpp::api().field_static_get_value(field, &max);
        if (max != rules::MaxFavor) throw std::runtime_error("game max favor changed; update the validated bounds");
    }, &why);
    Log("cof: %s%s", ready ? "metadata ready" : "unavailable: ", ready ? "" : why.c_str());
    if (ready) (void)tuning::Init();
    return ready;
}
TuningContext CurrentForTuning() {
    const auto c = Current(); return {c.actor, c.faction, SlotArray(c), Member(c.faction)};
}
std::string Read() {
    return Run([] {
        const auto c = Current(); Root data(ProphecyData()); std::ostringstream out;
        int rank = RankOf(c); void* rankArgs[]{&rank};
        Root available(Invoke(Resolve("ProphecyData", "GetAvailableRewardsForRank", 1), data.Get(), rankArgs));
        out << "{\"ok\":true,\"player\":{\"id\":" << Json(c.id) << ",\"name\":" << Json(c.name) << "},\"cof\":" << State(c)
            << ",\"favorMultiplier\":" << multiplier.load() << ",\"maxFavor\":" << rules::MaxFavor << ",\"maxCharges\":" << rules::MaxCharges
            << ",\"reputationMultiplier\":" << reputationMultiplier.load()
            << ",\"reputationAtMaxRank\":" << Boolean(rank >= Value<int>(Method(c.data, "get_MaxRank", 0), c.data))
            << ",\"reputationFromGainedFavor\":" << Get<float>(c.data, F("FactionData", "reputationGainMultiplierFromGainingExperience"))
            << ",\"reputationFromSpentFavor\":" << Get<float>(c.data, F("FactionData", "reputationGainMultiplierFromSpendingFavor"))
            << ",\"maxRank\":" << Value<int>(Method(c.data, "get_MaxRank", 0), c.data)
            << ",\"slots\":" << Slots(c) << ",\"effects\":{";
        const char* floats[]{"ChanceForDoubleEchoRewards0To1", "ChanceForDoubleEnemyRewards0To1", "ChanceToForceExaltedMultiplier",
            "ChanceToPreserveRuneOfAscendance0To1", "UniquesLPRollMultiplier", "IncreasedChanceToDropBossSpecificLoot"};
        bool effectFirst = true;
        for (const char* name : floats) {
            if (!effectFirst) out << ','; effectFirst = false;
            const auto field = std::string("<") + name + ">k__BackingField";
            out << Json(name) << ':' << Get<float>(c.faction, F("CircleOfFortune", field.c_str()));
        }
        const char* flags[]{"PropheciesGrantDoubleItems", "ExiledMagesDropExperimentalItemsTwice"};
        for (const char* name : flags) {
            const auto field = std::string("<") + name + ">k__BackingField";
            out << ',' << Json(name) << ':' << Boolean(Get<bool>(c.faction, F("CircleOfFortune", field.c_str())));
        }
        out << "},\"lenses\":[";
        for (int i = 0; i < 12; ++i) {
            if (i) out << ','; void* args[]{&i};
            out << "{\"id\":" << i << ",\"rankRequired\":" << Value<int>(Resolve("ProphecyData", "GetUnlockRankForLens", 1), data.Get(), args)
                << ",\"purchased\":" << Boolean(Value<bool>(Method(c.faction, "HasPurchasedLens", 1), c.faction, args))
                << ",\"name\":" << Json(Text(Invoke(Resolve("ProphecyData", "GetLocalizedLensName", 1), nullptr, args)))
                << ",\"effect\":" << Json(Text(Invoke(Resolve("ProphecyData", "GetLocalizedLensEffect", 1), nullptr, args))) << '}';
        }
        out << "],\"rewards\":["; bool first = true;
        for (void* reward : Array(Get<void*>(data.Get(), F("ProphecyData", "rewards")), 2048)) {
            if (!reward) continue; if (!first) out << ','; first = false; void* args[]{reward};
            const int unlock = Get<int>(reward, F("ProphecySlotReward", "unlocksAtRank"));
            void* containsArgs[]{reward};
            const bool selectable = Member(c.faction) && Value<bool>(Method(available.Get(), "Contains", 1), available.Get(), containsArgs);
            out << "{\"id\":" << RewardID(reward) << ",\"rankRequired\":" << unlock << ",\"available\":" << Boolean(selectable)
                << ",\"name\":" << Json(Text(Invoke(Resolve("ProphecyData", "GetLocalizedRewardName", 1), nullptr, args)))
                << ",\"baseFavorPerCharge\":" << Value<int>(Method(reward, "get_FavorPerCharge", 0), reward) << '}';
        }
        out << "],\"tuning\":" << tuning::Read() << '}'; return out.str();
    });
}
std::string Join(const std::string& id, bool replace) {
    return Mutation(id, "cofjoin", [=](const Context& c, bool apply) mutable -> std::string {
         if (Member(c.faction)) return std::string("{\"ok\":true,\"changed\":false}");
        void* merchant = GetFaction(c.provider, 1);
        if (Member(merchant) && !replace) throw std::runtime_error("Merchant's Guild is active; use the explicit switch option");
        if (!apply) return std::string();
        {
            if (Member(merchant)) { uint8_t mid = 1; void* args[]{&mid}; Invoke(Method(c.provider, "TryLeaveFaction", 1), c.provider, args);
                if (Member(merchant)) throw std::runtime_error("Merchant's Guild leave was refused"); }
            uint8_t fid = 0; void* args[]{&fid}; Invoke(Method(c.provider, "TryJoinFaction", 1), c.provider, args);
            if (!Member(c.faction)) throw std::runtime_error("the game's normal faction join conditions were not met");
            return std::string(",\"changed\":true");
        }
    });
}
std::string Rank(const std::string& id, int value) {
    return Mutation(id, "cofrank", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first"); const int max = Value<int>(Method(c.data, "get_MaxRank", 0), c.data);
        if (value < 1 || value > max) throw std::runtime_error("rank outside the runtime catalog");
        if (value == RankOf(c)) return "{\"ok\":true,\"changed\":false,\"cof\":" + State(c) + "}";
        if (!apply) return std::string();
        {
            int previous = RankOf(c);
            bool gameplay = false; void* args[]{&value, &gameplay}; Invoke(Method(c.faction, "SetRank", 2), c.faction, args);
            // This build's SetRank sorts the endpoints before ToggleRanks and
            // therefore enables bonuses even on a decrease. Reconcile the normal
            // rank toggle map/effects/slots in the actual decreasing direction.
            if (value < previous) {
                bool offlineLoad = false; void* toggleArgs[]{&previous, &value, &gameplay, &offlineLoad};
                Invoke(Method(c.faction, "ToggleRanks", 4), c.faction, toggleArgs);
            }
            int favor = Number(c.faction, "<Favor>k__BackingField"), rep = 0; void* values[]{&favor, &rep};
            Invoke(Method(c.faction, "SetFavorAndReputation", 2), c.faction, values);
            if (RankOf(c) != value) throw std::runtime_error("rank readback failed");
            return std::string(",\"changed\":true,\"slots\":") + Slots(c);
        }
    });
}
std::string Favor(const std::string& id, int value) {
    return Mutation(id, "coffavor", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first"); if (!rules::Favor(value)) throw std::runtime_error("favor must be 0..999999");
        if (!apply) return std::string();
        {
            int rep = Number(c.faction, "<Reputation>k__BackingField"); void* args[]{&value, &rep};
            Invoke(Method(c.faction, "SetFavorAndReputation", 2), c.faction, args);
            if (Number(c.faction, "<Favor>k__BackingField") != value) throw std::runtime_error("favor readback failed");
            return std::string();
        }
    });
}
std::string Reputation(const std::string& id, int amount) {
    return Mutation(id, "cofreputation", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first");
        if (amount < 0 || amount > 1000000) throw std::runtime_error("reputation grant must be 0..1000000");
        const int current = Number(c.faction, "<Reputation>k__BackingField");
        if (current < 0 || current > std::numeric_limits<int>::max() - amount)
            throw std::runtime_error("reputation addition would overflow");
        if (!apply) return std::string();
        {
            UnscaledReputation unscaled;
            void* args[]{&amount}; Invoke(Method(c.faction, "GainReputation", 1), c.faction, args); return std::string();
        }
    });
}
std::string UnlockLenses(const std::string& id) {
    return Mutation(id, "coflenses", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first"); Root data(ProphecyData());
        if (!apply) return std::string();
        {
            int unlocked = 0;
            for (int i = 0; i < 12; ++i) { void* args[]{&i};
                if (Value<int>(Resolve("ProphecyData", "GetUnlockRankForLens", 1), data.Get(), args) > RankOf(c) ||
                    Value<bool>(Method(c.faction, "HasPurchasedLens", 1), c.faction, args)) continue;
                if (!Value<bool>(Method(c.faction, "TryPurchaseLens", 1), c.faction, args)) throw std::runtime_error("lens unlock was refused");
                ++unlocked;
            }
            return ",\"unlocked\":" + std::to_string(unlocked);
        }
    });
}
std::string Configure(const std::string& id, int index, int reward, int lens, bool preview) {
    return Mutation(id, "cofprophecy", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first"); void* slot = Slot(c, index); Root config(Config(c, index, reward, lens));
        const bool reset = reward != RewardID(Get<void*>(slot, F("ProphecySlot", "<Reward>k__BackingField"))) || lens != LensID(slot);
        if (preview) {
            Root copy(Invoke(Method(slot, "Copy", 0), slot));
            struct NullableLens { bool hasValue; unsigned char padding[3]{}; int value; } nullable{lens >= 0, {}, lens};
            void* lensArgs[]{&nullable}; Invoke(Method(copy.Get(), "SetLens", 1), copy.Get(), lensArgs);
            void* rewardArgs[]{Reward(reward)}; Invoke(Method(copy.Get(), "SetReward", 1), copy.Get(), rewardArgs);
            const int before = Get<uint8_t>(slot, F("ProphecySlot", "<CompletedCharges>k__BackingField"));
            const int after = Get<uint8_t>(copy.Get(), F("ProphecySlot", "<CompletedCharges>k__BackingField"));
            return "{\"ok\":true,\"changesSelection\":" + std::string(Boolean(reset)) + ",\"currentCharges\":" +
                std::to_string(before) + ",\"chargesAfter\":" + std::to_string(after) + ",\"progressAfter\":" +
                std::to_string(Get<int>(copy.Get(), F("ProphecySlot", "<ChargeInProgress>k__BackingField"))) + "}";
        }
        if (!apply) return std::string();
        {
            void* args[]{config.Get()}; Invoke(Method(c.faction, "AttemptApplyProphecyConfiguration", 1), c.faction, args);
            void* selected = Slot(c, index);
            if (RewardID(Get<void*>(selected, F("ProphecySlot", "<Reward>k__BackingField"))) != reward || LensID(selected) != lens)
                throw std::runtime_error("prophecy configuration readback failed");
            return ",\"slots\":" + Slots(c);
        }
    });
}
std::string Charges(const std::string& id, int index, int value) {
    return Mutation(id, "cofcharges", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first"); void* slot = Slot(c, index);
        if (!rules::Charges(value)) throw std::runtime_error("charges must be 0..99");
        if (!Get<void*>(slot, F("ProphecySlot", "<Reward>k__BackingField"))) throw std::runtime_error("select a prophecy reward first");
        if (!apply) return std::string();
        {
            const int old = Get<uint8_t>(slot, F("ProphecySlot", "<CompletedCharges>k__BackingField"));
            int delta = value - old;
            if (delta < 0) { Invoke(Method(slot, "ResetCharges", 0), slot); delta = value; }
            if (delta > 0) { void* args[]{&delta}; Invoke(Method(slot, "AddCharges", 1), slot, args); }
            if (Get<uint8_t>(slot, F("ProphecySlot", "<CompletedCharges>k__BackingField")) != value) throw std::runtime_error("charge readback failed");
            return ",\"slots\":" + Slots(c);
        }
    });
}
std::string Multiplier(double value) {
    return Run([=] {
        if (!ready || !rules::Multiplier(value)) throw std::runtime_error("CoF favor multiplier must be finite and 1..100");
        if (value == 1) {
            multiplier = 1; std::string why;
            if (hook::IsInstalled(gain.code) && !hook::Remove(gain.code, &why)) throw std::runtime_error(why);
        } else {
            (void)Current({}, true); std::string why;
            if (!hook::IsInstalled(gain.code) && !hook::Install(gain.code, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&original), &why)) throw std::runtime_error(why);
            multiplier = value;
        }
        return "{\"ok\":true,\"favorMultiplier\":" + std::to_string(value) + "}";
    });
}
std::string ReputationMultiplier(double value) {
    return Run([=] {
        if (!ready || !rules::Multiplier(value)) throw std::runtime_error("CoF reputation multiplier must be finite and 1..100");
        if (value == 1) {
            reputationMultiplier = 1; std::string why;
            if (hook::IsInstalled(gainReputation.code) && !hook::Remove(gainReputation.code, &why)) throw std::runtime_error(why);
        } else {
            (void)Current({}, true); std::string why;
            if (!hook::IsInstalled(gainReputation.code) && !hook::Install(gainReputation.code,
                reinterpret_cast<void*>(&ReputationDetour), reinterpret_cast<void**>(&originalReputation), &why)) throw std::runtime_error(why);
            reputationMultiplier = value;
        }
        return "{\"ok\":true,\"reputationMultiplier\":" + std::to_string(value) + "}";
    });
}
std::string Status() {
    std::ostringstream out; out << "CoF favor: x" << multiplier.load() << ", hook " << (gain && hook::IsInstalled(gain.code) ? "in" : "out")
        << ", gains " << gains.load() << ", boosted " << boosted.load() << ", refused " << refused.load() << ", faults " << faults.load()
        << ", last " << lastIn.load() << " -> " << lastOut.load()
        << "\nCoF reputation: x" << reputationMultiplier.load()
        << ", hook " << (gainReputation && hook::IsInstalled(gainReputation.code) ? "in" : "out")
        << ", gains " << repGains.load() << ", boosted " << repBoosted.load() << ", refused " << repRefused.load()
        << ", faults " << repFaults.load() << ", max rank " << repAtCap.load() << ", manual " << repManual.load()
        << ", last " << repLastIn.load() << " -> " << repLastOut.load()
        << '\n' << tuning::Status(); return out.str();
}
#ifdef EPOCHPACT_RESEARCH
std::string TestGain(const std::string& id, int amount, bool includeReputation) {
    return Mutation(id, "coffavor", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first");
        if (c.name != "EpCoFTest" || amount < 1 || amount > 10000) throw std::runtime_error("gain probe requires isolated EpCoFTest and 1..10000");
        if (!apply) return std::string();
        { bool ignoreRep = !includeReputation, ignoreMult = false; void* args[]{&amount, &ignoreRep, &ignoreMult};
            Invoke(gain, c.faction, args); return ",\"slots\":" + Slots(c); }
    });
}
std::string TestReputation(const std::string& id, int amount, bool otherFaction) {
    return Mutation(id, "cofreputation", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first");
        if (c.name != "EpCoFTest" || amount < 1 || amount > 10000) throw std::runtime_error("reputation probe requires isolated EpCoFTest and 1..10000");
        if (!apply) return std::string();
        {
            void* target = otherFaction ? GetFaction(c.provider, 1) : c.faction;
            void* args[]{&amount}; Invoke(gainReputation, target, args); return std::string();
        }
    });
}
std::string TestSpend(const std::string& id, int amount) {
    return Mutation(id, "coffavor", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first");
        if (c.name != "EpCoFTest" || amount < 1 || amount > 10000) throw std::runtime_error("spend probe requires isolated EpCoFTest and 1..10000");
        if (!apply) return std::string();
        {
            bool grantRep = true; void* args[]{&amount, &grantRep};
            if (!Value<bool>(Method(c.faction, "TrySpendFavor", 2), c.faction, args)) throw std::runtime_error("normal Favor spend refused");
            return std::string();
        }
    });
}
std::string TestCharge(const std::string& id, int index, int amount) {
    return Mutation(id, "cofcharges", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first");
        if (c.name != "EpCoFTest" || amount < 1 || amount > 10000)
            throw std::runtime_error("charge probe requires isolated EpCoFTest and 1..10000");
        (void)Slot(c, index);
        if (!apply) return std::string();
        {
            void* args[]{&index, &amount}; Invoke(Method(c.faction, "AddFavorToSlot", 2), c.faction, args);
            return ",\"slots\":" + Slots(c);
        }
    });
}
std::string TestReward(const std::string& id, int index, int target) {
    return Mutation(id, "cofcharges", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first"); void* slot = Slot(c, index);
        if (c.name != "EpCoFTest" || target < 0 || target > 8)
            throw std::runtime_error("reward probe requires isolated EpCoFTest and a normal target 0..8");
        if (!apply) return std::string();
        {
            bool doubleItems = Get<bool>(c.faction, F("CircleOfFortune", "<PropheciesGrantDoubleItems>k__BackingField"));
            int used = 0; void* args[]{&target, &doubleItems, c.actor, &used};
            const bool triggered = Value<bool>(Method(slot, "TryTriggerReward", 4), slot, args);
            return ",\"triggered\":" + std::string(Boolean(triggered)) + ",\"chargesUsed\":" + std::to_string(used) + ",\"slots\":" + Slots(c);
        }
    });
}
std::string TestLoot(const std::string& id, const std::string& kind, int count) {
    return Mutation(id, "cofcharges", [=](const Context& c, bool apply) mutable -> std::string {
        if (!Member(c.faction)) throw std::runtime_error("join Circle of Fortune first");
        if (c.name != "EpCoFTest" || count < 1 || count > (kind == "level" ? 100 : 30) ||
            (kind != "enemy" && kind != "echo" && kind != "lp" && kind != "level"))
            throw std::runtime_error("loot probe requires isolated EpCoFTest and a bounded count/level");
        if (!apply) return std::string();
        {
            if (kind == "level") {
                void* experience = Get<void*>(c.actor, Offset("Actor", "experienceTracker"));
                int level = count; void* args[]{&level}; Invoke(Method(experience, "SetLevel", 1), experience, args);
                return ",\"level\":" + std::to_string(Value<int>(Method(experience, "get_CurrentLevel", 0), experience));
            }
            if (kind == "lp") {
                Root item(il2cpp::api().object_new(game::FindClass("LE.dll", "", "ItemDataUnpacked")));
                Invoke(Resolve("ItemDataUnpacked", ".ctor", 0, ""), item.Get());
                uint16_t uniqueID = 1; void* args[]{&uniqueID};
                Root unique(Invoke(Resolve("UniqueList", "getUnique", 1, ""), nullptr, args));
                void* lpArgs[]{unique.Get(), c.actor};
                const float result = Value<float>(Resolve("ItemData", "GetUniqueLPRollMultiplierFromCoF", 2, ""), item.Get(), lpArgs);
                return ",\"lpRollMultiplier\":" + std::to_string(result);
            }
            const auto transformMethod = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
            const auto positionMethod = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_position", 0);
            Root transform(Invoke(transformMethod, c.actor));
            struct Position { float x, y, z; } position = Value<Position>(positionMethod, transform.Get());
            void* experience = Get<void*>(c.actor, Offset("Actor", "experienceTracker"));
            const int level = Value<int>(Method(experience, "get_CurrentLevel", 0), experience);
            if (kind == "enemy") {
                const int scene = Value<int>(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine.SceneManagement", "SceneManager", "GetActiveScene", 0));
                float chance = 1, itemMult = 1, gold = 0, crafting = 0, radius = .5f;
                int baseRates = 0, flags = 1, goldType = 0, corrupt = 0;
                bool compatible = false, additional = false, death = true, craftingOnly = false, onePile = true;
                void* args[]{const_cast<int*>(&level), &position, &chance, &compatible, &itemMult, &baseRates, &additional, &gold, &gold,
                    &crafting, &flags, const_cast<int*>(&scene), &death, &craftingOnly, &onePile, &goldType, &corrupt, &radius};
                for (int i = 0; i < count; ++i) Invoke(Resolve("ItemDrop", "DropItem", 18, ""), nullptr, args);
            } else {
                Root catalog(Invoke(Resolve("DroppableRewardList", "get", 0, "")));
                const auto rewards = Entries(Get<void*>(catalog.Get(), Offset("DroppableRewardList", "monolithRewards")));
                // Use a normal catalog reward whose item rolls are guaranteed.
                // Probabilistic rolls cannot assert an exact 1:2 ground count.
                int rewardIndex = -1, attempts = 0;
                for (int i = 0; i < static_cast<int>(rewards.size()); ++i) {
                    const int type = Get<int>(rewards[i], Offset("DroppableReward", "rewardType"));
                    if (type == 1 || type == 2 || type == 18) continue; // Gold, XP, crafting reward special paths.
                    const auto rolls = Entries(Get<void*>(rewards[i], Offset("DroppableReward", "Rolls")));
                    bool guaranteed = !rolls.empty(); int total = 0;
                    for (void* roll : rolls) {
                        const int n = Get<int>(roll, Offset("DroppableReward.Roll", "Attempts"));
                        const float chance = Get<float>(roll, Offset("DroppableReward.Roll", "Chance"));
                        void* info = Get<void*>(roll, Offset("DroppableReward.Roll", "DropInfo"));
                        guaranteed &= n > 0 && chance == 1 && info &&
                            Get<int>(info, Offset("DroppableReward.DropInformation", "Type")) == 0;
                        total += n;
                    }
                    if (guaranteed && total > 0 && total <= 30) { rewardIndex = i; attempts = total; break; }
                }
                if (rewardIndex < 0) throw std::runtime_error("guaranteed normal item echo reward unavailable");
                Root reward(il2cpp::api().object_new(game::FindClass("LE.dll", "", "EchoCompletionReward")));
                int timeline = 1, islandType = 0, corruption = 0, difficulty = 0, stability = 0, chests = 0, guestIndex = 0;
                float score = 0, depth = 0, countModifier = 0; bool specific = true, guestSpecific = false;
                void* ctorArgs[]{nullptr, &timeline, &islandType, const_cast<int*>(&level), &corruption, &score, &difficulty, &stability,
                    &depth, &chests, &specific, &rewardIndex, &guestSpecific, &guestIndex, &guestIndex, &countModifier};
                Invoke(Resolve("EchoCompletionReward", ".ctor", 16, ""), reward.Get(), ctorArgs);
                bool extra = false; int overrideIndex = -1, source = 1, sourceIndex = 0;
                void* args[]{c.actor, &position, reward.Get(), &extra, &overrideIndex, &source, &sourceIndex};
                void* sync = nullptr; void* syncArgs[]{&sync};
                if (!Value<bool>(Method(c.actor, "TryGetPlayerActorSync", 1), c.actor, syncArgs) || !sync)
                    throw std::runtime_error("isolated player sync unavailable");
                Root playerSync(sync); void* resetArgs[]{playerSync.Get()};
                for (int i = 0; i < count; ++i) {
                    // The normal game permits one echo reward per player until
                    // the next run resets its claim guard. Simulate that normal
                    // boundary only in this isolated test, never in the hook.
                    Invoke(Resolve("MonolithItemManager", "ResetPlayerRewards", 1, "LE.Gameplay.Monolith"), nullptr, resetArgs);
                    Invoke(Resolve("MonolithItemManager", "SpawnEchoSpecificRewards", 7, "LE.Gameplay.Monolith"), nullptr, args);
                }
                return ",\"probe\":\"echo\",\"count\":" + std::to_string(count) +
                    ",\"rewardIndex\":" + std::to_string(rewardIndex) + ",\"guaranteedAttempts\":" + std::to_string(attempts);
            }
            return std::string(",\"probe\":") + Json(kind) + ",\"count\":" + std::to_string(count);
        }
    });
}
std::string TestGround(const std::string& id) {
    return Run([=] {
        const auto c = Edit(id);
        if (c.name != "EpCoFTest") throw std::runtime_error("ground probe requires isolated EpCoFTest");
        const auto* field = game::FindStaticField("LE.dll", "", "ItemTooltipOrganizer", "pickableGroundLabelList");
        void* labels = field ? game::StaticObject(field) : nullptr;
        std::ostringstream out; out << "{\"ok\":true,\"items\":["; bool first = true;
        if (labels) {
            const auto* listField = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(labels), "_list");
            if (!listField) throw std::runtime_error("ground label collection unavailable");
            void* list = Get<void*>(labels, il2cpp::api().field_get_offset(listField));
            const auto* labelClass = game::FindClass("LE.dll", "", "GroundItemLabel");
            for (void* label : Entries(list)) {
                if (!label || il2cpp::api().object_get_class(label) != labelClass || !game::IsAlive(label)) continue;
                void* item = Invoke(Method(label, "getItemData", 0), label); if (!item) continue;
                if (!first) out << ','; first = false;
                out << "{\"id\":" << Json(std::to_string(reinterpret_cast<uintptr_t>(item)))
                    << ",\"rarity\":" << static_cast<int>(Get<uint8_t>(item, Offset("ItemData", "rarity")))
                    << ",\"affixTiers\":["; bool affixFirst = true;
                void* affixes = Get<void*>(item, Offset("ItemData", "affixes"));
                if (affixes) for (void* affix : Entries(affixes)) {
                    if (!affixFirst) out << ','; affixFirst = false;
                    out << static_cast<int>(Get<uint8_t>(affix, Offset("ItemAffix", "affixTier")));
                }
                out << "]}";
            }
        }
        out << "]}"; return out.str();
    });
}
#endif
}
