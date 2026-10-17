#include "rage.hpp"
#include "rage_rules.hpp"
#include "managed.hpp"
#include "common.hpp"
#include "lifecycle.hpp"
#include "hook.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

// Rage of Morditas lives in the game's own data: RandomEncounterData (how often the
// encounter is placed), RageOfMorditasData (Bloodrage charge and hunt buff kill
// thresholds, the chance of Blood rather than Ice forging potential) and one RageOfMorditasEncounterConfig
// per level band (hunt duration and the time each kill adds). The settings scale the
// values the game loaded; the first values seen for each object are kept and written
// back when a setting returns to normal or online play starts. Nothing is saved.
namespace ep::rage {
namespace {
using namespace managed;
enum Key { Spawn, HuntTime, Charges, Buffs, Blood, Count };
struct Setting { const char* name; rules::Range range; std::atomic<double> value; };
Setting settings[Count]{{"spawn", rules::kSpawn, 1}, {"hunttime", rules::kHuntTime, 1}, {"charges", rules::kCharges, 1},
    {"buffs", rules::kBuffs, 1}, {"blood", rules::kBlood, -1}};
struct Fields {
    size_t data, campaign, monolith, minimumLevel, configs, chargeKills, buffKills, blood, iceIfNotBlood, iceRitual, bloodRitual;
    size_t type, minLevel, maxLevel, initial, perKill, champion;
    size_t state, kills, charges, elapsed, finalTime, buffs;
} f{};
game::MethodRef encounterData, componentTransform, transformPosition;
const il2cpp::Field* runningInstance = nullptr;
bool ready = false;
// The game's own values, as first seen per object.
struct EncounterOriginal { std::unique_ptr<Root> object; float campaign = 0, monolith = 0; } encounter;
struct DataOriginal { std::unique_ptr<Root> object; std::vector<int> charges, buffs; std::array<float, 4> forging{}; } data;
struct ConfigOriginal { std::unique_ptr<Root> object; float initial = 0, champion = 0; std::array<float, 3> perKill{}; std::array<bool, 3> hasKill{}; };
std::vector<ConfigOriginal> configs;
bool applied = false;  // the game's data may differ from its own values
uint64_t nextCheck = 0, faults = 0, writes = 0;
std::string lastError;
constexpr int kRarities = 3;  // Spawner.MonsterRarity: Common, Magic, Rare

bool Active() { for (auto& s : settings) if (s.value.load() != s.range.neutral) return true; return false; }
template<class T> void Put(void* object, size_t offset, T value) {
    T& field = *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
    if (field != value) { field = value; ++writes; }
}
struct IntList { int* items = nullptr; int size = 0; };
IntList Ints(void* list) {
    if (!list) return {};
    auto& a = il2cpp::api();
    const auto* cls = a.object_get_class(list);
    const auto* sizeField = a.class_get_field_from_name(cls, "_size");
    const auto* itemsField = a.class_get_field_from_name(cls, "_items");
    if (!sizeField || !itemsField) throw std::runtime_error("Rage of Morditas kill threshold layout unavailable.");
    const int n = Get<int>(list, a.field_get_offset(sizeField));
    void* array = Get<void*>(list, a.field_get_offset(itemsField));
    if (n < 0 || n > 64 || (n && !array) || (array && static_cast<uintptr_t>(n) > Get<uintptr_t>(array, 0x18)))
        throw std::runtime_error("Rage of Morditas kill thresholds outside supported bounds.");
    return {array ? reinterpret_cast<int*>(static_cast<char*>(array) + 0x20) : nullptr, n};
}
int ListSize(void* list) {
    if (!list) return 0;
    const auto* size = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(list), "_size");
    return size ? Get<int>(list, il2cpp::api().field_get_offset(size)) : -1;
}
std::vector<int> Copy(const IntList& list) { return std::vector<int>(list.items, list.items + list.size); }
std::vector<void*> Configs(void* dictionary) {
    std::vector<void*> out;
    if (!dictionary) return out;
    Root values(Invoke(Method(dictionary, "get_Values", 0), dictionary));
    const int count = Value<int>(Method(values.Get(), "get_Count", 0), values.Get());
    if (count < 0 || count > 64) throw std::runtime_error("Rage of Morditas encounter configs outside supported bounds.");
    Root array(il2cpp::api().array_new(game::FindClass("LE.dll", "", "RageOfMorditasEncounterConfig"), static_cast<uintptr_t>(count)));
    int first = 0; void* args[]{array.Get(), &first};
    Invoke(Method(values.Get(), "CopyTo", 2), values.Get(), args);
    for (int i = 0; i < count; ++i) if (void* c = Get<void*>(array.Get(), 0x20 + sizeof(void*) * i)) out.push_back(c);
    return out;
}
bool TryFloat(void* dictionary, int key, float* value) { void* args[]{&key, value}; return Value<bool>(Method(dictionary, "TryGetValue", 2), dictionary, args); }
void SetFloat(void* dictionary, int key, float value) {
    float current = 0;
    if (TryFloat(dictionary, key, &current) && current == value) return;
    void* args[]{&key, &value}; Invoke(Method(dictionary, "set_Item", 2), dictionary, args); ++writes;
}
// The game's data now, its own values kept the first time each object is seen.
void* Capture() {
    void* red = Invoke(encounterData);
    if (!red || !game::IsAlive(red)) throw std::runtime_error("Random encounter data unavailable.");
    if (!encounter.object || encounter.object->Get() != red)
        encounter = {std::make_unique<Root>(red), Get<float>(red, f.campaign), Get<float>(red, f.monolith)};
    void* d = Get<void*>(red, f.data);
    if (!d || !game::IsAlive(d)) throw std::runtime_error("Rage of Morditas data unavailable.");
    if (!data.object || data.object->Get() != d) {
        data.object = std::make_unique<Root>(d);
        data.charges = Copy(Ints(Get<void*>(d, f.chargeKills)));
        data.buffs = Copy(Ints(Get<void*>(d, f.buffKills)));
        data.forging = {Get<float>(d, f.blood), Get<float>(d, f.iceIfNotBlood), Get<float>(d, f.iceRitual), Get<float>(d, f.bloodRitual)};
        configs.clear();
    }
    for (void* c : Configs(Get<void*>(d, f.configs))) {
        bool known = false;
        for (auto& o : configs) known |= o.object->Get() == c;
        if (known) continue;
        ConfigOriginal o;
        o.object = std::make_unique<Root>(c);
        o.initial = Get<float>(c, f.initial); o.champion = Get<float>(c, f.champion);
        if (void* kill = Get<void*>(c, f.perKill)) for (int r = 0; r < kRarities; ++r) o.hasKill[r] = TryFloat(kill, r, &o.perKill[r]);
        configs.push_back(std::move(o));
    }
    return d;
}
void WriteThresholds(void* list, const std::vector<int>& original, double share) {
    auto now = Ints(list);
    if (now.size != static_cast<int>(original.size())) throw std::runtime_error("Rage of Morditas kill thresholds changed size.");
    const auto wanted = share == 1 ? original : rules::ScaleThresholds(original, share);
    for (int i = 0; i < now.size; ++i) if (now.items[i] != wanted[i]) { now.items[i] = wanted[i]; ++writes; }
}
// Every value from the game's own one and the settings (the settings at normal write the originals).
void Apply() {
    void* d = Capture();
    void* red = encounter.object->Get();
    const double spawn = settings[Spawn].value, hunt = settings[HuntTime].value, charges = settings[Charges].value,
        buffs = settings[Buffs].value, blood = settings[Blood].value;
    Put(red, f.campaign, spawn == 1 ? encounter.campaign : rules::ScaleRate(encounter.campaign, spawn));
    Put(red, f.monolith, spawn == 1 ? encounter.monolith : rules::ScaleRate(encounter.monolith, spawn));
    WriteThresholds(Get<void*>(d, f.chargeKills), data.charges, charges);
    WriteThresholds(Get<void*>(d, f.buffKills), data.buffs, buffs);
    // Blood share: only the chance an item gets Blood rather than Ice (the rituals keep forcing their own).
    Put(d, f.blood, rules::BloodChance(data.forging[0], blood));
    for (auto& o : configs) {
        void* c = o.object->Get();
        if (!c || !game::IsAlive(c)) continue;
        Put(c, f.initial, hunt == 1 ? o.initial : rules::ScaleTime(o.initial, hunt));
        Put(c, f.champion, hunt == 1 ? o.champion : rules::ScaleTime(o.champion, hunt));
        if (void* kill = Get<void*>(c, f.perKill))
            for (int r = 0; r < kRarities; ++r) if (o.hasKill[r]) SetFloat(kill, r, hunt == 1 ? o.perKill[r] : rules::ScaleTime(o.perKill[r], hunt));
    }
    applied = Active();
}
// The game's own values back (online play, or every setting at normal).
void Restore() {
    if (!encounter.object || !data.object) { applied = false; return; }
    void* red = encounter.object->Get();
    void* d = data.object->Get();
    if (red && game::IsAlive(red)) { Put(red, f.campaign, encounter.campaign); Put(red, f.monolith, encounter.monolith); }
    if (d && game::IsAlive(d)) {
        WriteThresholds(Get<void*>(d, f.chargeKills), data.charges, 1);
        WriteThresholds(Get<void*>(d, f.buffKills), data.buffs, 1);
        const size_t forgingFields[]{f.blood, f.iceIfNotBlood, f.iceRitual, f.bloodRitual};
        for (int i = 0; i < 4; ++i) Put(d, forgingFields[i], data.forging[i]);
    }
    for (auto& o : configs) {
        void* c = o.object->Get();
        if (!c || !game::IsAlive(c)) continue;
        Put(c, f.initial, o.initial); Put(c, f.champion, o.champion);
        if (void* kill = Get<void*>(c, f.perKill)) for (int r = 0; r < kRarities; ++r) if (o.hasKill[r]) SetFloat(kill, r, o.perKill[r]);
    }
    applied = false;
}
void Write(std::ostream& out, const std::vector<int>& v) { out << '['; for (size_t i = 0; i < v.size(); ++i) out << (i ? "," : "") << v[i]; out << ']'; }
}  // namespace
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
void* SceneEncounterForRead();  // test builds: the scene's encounter before it runs
#endif
namespace {
std::string State() {
    std::ostringstream out; out.setf(std::ios::fixed); out.precision(3);
    out << "{\"ok\":true,\"available\":" << Boolean(ready) << ",\"offline\":" << Boolean(game::IsOfflinePlay()) << ",\"settings\":{";
    for (int k = 0; k < Count; ++k) out << (k ? "," : "") << '"' << settings[k].name << "\":{\"value\":" << settings[k].value.load() << ",\"available\":" << Boolean(ready) << '}';
    out << "},\"applied\":" << Boolean(applied) << ",\"writes\":" << writes << ",\"faults\":" << faults << ",\"error\":" << Json(lastError);
    if (ready && game::IsOfflinePlay()) {
        void* d = Capture(); void* red = encounter.object->Get();
        out << ",\"game\":{\"minimumLevel\":" << Get<float>(red, f.minimumLevel) << ",\"campaignRate\":" << Get<float>(red, f.campaign) << ",\"monolithRate\":" << Get<float>(red, f.monolith)
            << ",\"chargeKills\":"; Write(out, Copy(Ints(Get<void*>(d, f.chargeKills))));
        out << ",\"buffKills\":"; Write(out, Copy(Ints(Get<void*>(d, f.buffKills))));
        out << ",\"forging\":[" << Get<float>(d, f.blood) << ',' << Get<float>(d, f.iceIfNotBlood) << ',' << Get<float>(d, f.iceRitual) << ',' << Get<float>(d, f.bloodRitual) << "],\"configs\":[";
        bool comma = false;
        for (auto& o : configs) {
            void* c = o.object->Get(); if (!c || !game::IsAlive(c)) continue;
            out << (comma ? "," : "") << "{\"type\":" << Get<int>(c, f.type) << ",\"levels\":[" << Get<int>(c, f.minLevel) << ',' << Get<int>(c, f.maxLevel) << "],\"huntSeconds\":" << Get<float>(c, f.initial)
                << ",\"championSeconds\":" << Get<float>(c, f.champion) << ",\"killSeconds\":[";
            void* kill = Get<void*>(c, f.perKill);
            for (int r = 0; r < kRarities; ++r) { float v = 0; out << (r ? "," : "") << (kill && TryFloat(kill, r, &v) ? v : 0.f); }
            out << "]}"; comma = true;
        }
        out << "]},\"original\":{\"campaignRate\":" << encounter.campaign << ",\"monolithRate\":" << encounter.monolith << ",\"chargeKills\":"; Write(out, data.charges);
        out << ",\"buffKills\":"; Write(out, data.buffs);
        out << ",\"forging\":[" << data.forging[0] << ',' << data.forging[1] << ',' << data.forging[2] << ',' << data.forging[3] << "],\"configs\":[";
        for (size_t i = 0; i < configs.size(); ++i) {
            const auto& o = configs[i];
            out << (i ? "," : "") << "{\"huntSeconds\":" << o.initial << ",\"championSeconds\":" << o.champion << ",\"killSeconds\":[" << o.perKill[0] << ',' << o.perKill[1] << ',' << o.perKill[2] << "]}";
        }
        out << "]}";
        void* running = runningInstance ? game::StaticObject(runningInstance) : nullptr;
#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
        if (!running || !game::IsAlive(running)) running = SceneEncounterForRead();
#endif
        if (running && game::IsAlive(running))
            out << ",\"encounter\":{\"state\":" << Get<int>(running, f.state) << ",\"kills\":" << Get<int>(running, f.kills) << ",\"charges\":" << Get<int>(running, f.charges)
                << ",\"elapsed\":" << Get<float>(running, f.elapsed) << ",\"finalTime\":" << Get<float>(running, f.finalTime) << ",\"buffs\":" << ListSize(Get<void*>(running, f.buffs)) << '}';
        else out << ",\"encounter\":null";
    }
    out << '}';
    return out.str();
}
}  // namespace

bool Init() {
    try {
        encounterData = game::FindMethod("LE.dll", "", "RandomEncounterData", "get", 0);
        componentTransform = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
        transformPosition = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_position", 0);
        runningInstance = game::FindStaticField("LE.dll", "", "RageOfMorditasLogic", "runningInstance");
        f.data = Offset("RandomEncounterData", "rageOfMorditasData"); f.campaign = Offset("RandomEncounterData", "baseRageOfMorditasCampaignSpawnRate");
        f.monolith = Offset("RandomEncounterData", "baseRageOfMorditasMonolithSpawnRate"); f.minimumLevel = Offset("RandomEncounterData", "minimumLevelForRageOfMorditas");
        f.configs = Offset("RageOfMorditasData", "EncounterConfigs"); f.chargeKills = Offset("RageOfMorditasData", "HuntKillThresholdsForBloodrageCharges");
        f.buffKills = Offset("RageOfMorditasData", "HuntKillThresholdsForPlayerBuffs"); f.blood = Offset("RageOfMorditasData", "chanceForBloodForgingPotential");
        f.iceIfNotBlood = Offset("RageOfMorditasData", "chanceForIceForgingPotentialIfNotBlood"); f.iceRitual = Offset("RageOfMorditasData", "iceForgingPotentialChanceWithIceRitual");
        f.bloodRitual = Offset("RageOfMorditasData", "bloodForgingPotentialChanceWithBloodRitual");
        f.type = Offset("RageOfMorditasEncounterConfig", "Type"); f.minLevel = Offset("RageOfMorditasEncounterConfig", "MinLevel"); f.maxLevel = Offset("RageOfMorditasEncounterConfig", "MaxLevel");
        f.initial = Offset("RageOfMorditasEncounterConfig", "HuntInitialDuration"); f.perKill = Offset("RageOfMorditasEncounterConfig", "TimeAddedToHuntDurationOnKill");
        f.champion = Offset("RageOfMorditasEncounterConfig", "TimeAddedToHuntDurationOnChampionKill");
        f.state = Offset("RageOfMorditasLogic", "_encounterState"); f.kills = Offset("RageOfMorditasLogic", "_currentHuntKillCount"); f.charges = Offset("RageOfMorditasLogic", "_bloodrageChargeCount");
        f.elapsed = Offset("RageOfMorditasLogic", "_huntElapsedTime"); f.finalTime = Offset("RageOfMorditasLogic", "_huntFinalTime"); f.buffs = Offset("RageOfMorditasLogic", "_activeBuffs");
        ready = encounterData && componentTransform && transformPosition && runningInstance;
    } catch (const std::exception& e) { ready = false; lastError = e.what(); }
    Log("rage: Rage of Morditas settings %s%s%s", ready ? "ready" : "unavailable", lastError.empty() ? "" : ": ", lastError.c_str());
    return ready;
}
void Frame() {
    if (!ready || lifecycle::Stopping()) return;
    const auto now = GetTickCount64();
    if (now < nextCheck) return;
    nextCheck = now + 1000;
    if (!Active() && !applied) return;  // nothing changed: no game calls at all
    std::string why;
    if (!game::Guarded([] { if (game::IsOfflinePlay()) Apply(); else Restore(); }, &why)) {
        ++faults; lastError = why; nextCheck = now + 10000;
        Log("rage: %s", why.c_str());
    }
}
std::string Read() { return Run([] { return State(); }); }
std::string Set(const std::string& key, double value) {
    int k = 0;
    while (k < Count && key != settings[k].name) ++k;
    if (k == Count) return "{\"ok\":false,\"error\":\"use rageset spawn|hunttime|charges|buffs|blood value\"}";
    if (!rules::InRange(settings[k].range, value)) {
        std::ostringstream e; e << "{\"ok\":false,\"error\":\"" << settings[k].name << " must be " << settings[k].range.low << " to " << settings[k].range.high << "\"}";
        return e.str();
    }
    if (!ready) return "{\"ok\":false,\"error\":\"Rage of Morditas data is unavailable in this game build.\"}";
    return Run([k, value] {
        const double previous = settings[k].value.exchange(value);
        try { if (game::IsOfflinePlay()) Apply(); else if (applied) Restore(); }
        catch (...) { settings[k].value = previous; throw; }
        lastError.clear();
        return State();
    });
}
void ReleaseRoots() { encounter = {}; data = {}; configs.clear(); applied = false; }

#if defined(EPOCHPACT_RESEARCH) || defined(EPOCHPACT_TESTING)
namespace {
struct Vec3 { float x, y, z; };
Vec3 Position(void* component) { Root t(Invoke(componentTransform, component)); return Value<Vec3>(transformPosition, t.Get()); }
void* TestPlayer(const std::string& expectedId) {
    if (!game::IsOfflinePlay()) throw std::runtime_error("Offline play only.");
    void* actor = Invoke(game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0));
    if (!actor || !game::IsAlive(actor)) throw std::runtime_error("Load an offline character first.");
    void* charData = Get<void*>(Get<void*>(actor, Offset("Actor", "characterDataTracker")), Offset("CharacterDataTracker", "charData"));
    if (!charData || Text(Invoke(Method(charData, "get_Id", 0), charData)) != expectedId ||
        Text(Get<void*>(charData, Offset("CharacterData", "<CharacterName>k__BackingField", "LE.Data"))) != "EpFeedbackTest")
        throw std::runtime_error("Rage of Morditas checks require the isolated EpFeedbackTest character.");
    return actor;
}
// The game's own placement decisions while the probe is in (test only).
using CheckFn = bool (*)(void*, void*, const il2cpp::Method*);
using SpawnFn = void (*)(void*, void*, const il2cpp::Method*);
using PlayerSpawnFn = void (*)(void*, void*, void*, const il2cpp::Method*);
using PlaceFn = void (*)(void*, void*, void*, const il2cpp::Method*);  // the Vector3 goes by reference
CheckFn originalFeature = nullptr, originalCan = nullptr;
SpawnFn originalSpawn = nullptr;
PlayerSpawnFn originalPlayerSpawn = nullptr;
PlaceFn originalPlace = nullptr;
std::atomic<int> featureCalls{0}, featureTrue{0}, canCalls{0}, canTrue{0}, spawnCalls{0}, playerSpawnCalls{0}, placeCalls{0};
std::atomic<float> lastZoneFactor{-1};  // PlacementConfigData.SpawnRateMultiplier of the last roll
bool ProbeFeature(void* self, void* placement, const il2cpp::Method* m) { const bool r = originalFeature(self, placement, m); ++featureCalls; if (r) ++featureTrue; return r; }
bool ProbeCan(void* self, void* placement, const il2cpp::Method* m) { const bool r = originalCan(self, placement, m); ++canCalls; if (r) ++canTrue; return r; }
void ProbeSpawn(void* self, void* placement, const il2cpp::Method* m) {
    ++spawnCalls;
    // The roll is: rate x placement.SceneData.Config.SpawnRateMultiplier (x the Weaver bonus in echoes).
    if (void* scene = placement ? Get<void*>(placement, Offset("RandomEncounterTypes.EncounterPlacementData", "<SceneData>k__BackingField")) : nullptr)
        if (void* config = Get<void*>(scene, Offset("RandomEncounterTypes.SceneData", "<Config>k__BackingField")))
            lastZoneFactor = Get<float>(config, Offset("RandomEncounterTypes.PlacementConfigData", "<SpawnRateMultiplier>k__BackingField"));
    originalSpawn(self, placement, m);
}
void ProbePlace(void* self, void* placement, void* at, const il2cpp::Method* m) { ++placeCalls; originalPlace(self, placement, at, m); }
void ProbePlayerSpawn(void* self, void* player, void* placement, const il2cpp::Method* m) { ++playerSpawnCalls; originalPlayerSpawn(self, player, placement, m); }
void InstallProbe() {
    std::string why;
    const auto install = [&](const char* name, int arity, void* detour, void** original) {
        auto m = game::FindMethod("LE.dll", "", "RandomRageOfMorditasSpawner", name, arity);
        if (!m) throw std::runtime_error(std::string("missing ") + name);
        if (!hook::IsInstalled(m.code) && !hook::Install(m.code, detour, original, &why)) throw std::runtime_error(why);
    };
    install("IsFeatureEnabled", 1, reinterpret_cast<void*>(&ProbeFeature), reinterpret_cast<void**>(&originalFeature));
    install("CanSpawn", 1, reinterpret_cast<void*>(&ProbeCan), reinterpret_cast<void**>(&originalCan));
    install("ExecuteSpawn", 1, reinterpret_cast<void*>(&ProbeSpawn), reinterpret_cast<void**>(&originalSpawn));
    install("ExecutePlayerDependentSpawn", 2, reinterpret_cast<void*>(&ProbePlayerSpawn), reinterpret_cast<void**>(&originalPlayerSpawn));
    install("PlaceNearPlayerSpawn", 2, reinterpret_cast<void*>(&ProbePlace), reinterpret_cast<void**>(&originalPlace));
}
std::vector<void*> SceneObjects(const char* name) {
    const auto* cls = game::FindClass("LE.dll", "", name);
    if (!cls) throw std::runtime_error(std::string("Type unavailable: ") + name);
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
    bool inactive = false; void* args[]{type.Get(), &inactive};
    Root array(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, args));
    std::vector<void*> out;
    const auto n = array.Get() ? Get<uintptr_t>(array.Get(), 0x18) : 0;
    for (uintptr_t i = 0; i < n && i < 64; ++i) if (void* o = Get<void*>(array.Get(), 0x20 + 8 * i)) out.push_back(o);
    return out;
}
// The encounter in this scene (the game sets runningInstance only once it runs).
void* SceneEncounter() { const auto all = SceneObjects("RageOfMorditasLogic"); return all.empty() ? nullptr : all.front(); }
}  // namespace
void* SceneEncounterForRead() { return SceneEncounter(); }
namespace {
std::vector<void*> Crystals() {
    const auto* cls = game::FindClass("LE.dll", "", "RageOfMorditasInteractableObject");
    if (!cls) throw std::runtime_error("Crystal type unavailable.");
    Root type(il2cpp::api().type_get_object(il2cpp::api().class_get_type(cls)));
    bool inactive = false; void* args[]{type.Get(), &inactive};
    Root array(Invoke(game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "FindObjectsOfType", 2), nullptr, args));
    std::vector<void*> out;
    const auto n = array.Get() ? Get<uintptr_t>(array.Get(), 0x18) : 0;
    for (uintptr_t i = 0; i < n && i < 64; ++i) if (void* o = Get<void*>(array.Get(), 0x20 + 8 * i)) out.push_back(o);
    return out;
}
}  // namespace
std::string Test(const std::string& expectedId, const std::string& action) {
    return Run([=]() -> std::string {
        void* player = TestPlayer(expectedId);
        void* running = game::StaticObject(runningInstance);
        if (action == "crystals") {
            // Read only: the crystals in this scene, where they are, and the running encounter.
            const auto me = Position(player);
            std::ostringstream out; out.setf(std::ios::fixed); out.precision(1);
            out << "{\"ok\":true,\"me\":[" << me.x << ',' << me.z << "],\"crystals\":[";
            bool comma = false;
            for (void* c : Crystals()) { const auto p = Position(c); out << (comma ? "," : "") << '[' << p.x << ',' << p.z << ']'; comma = true; }
            // The character's cycle (1 Legacy) and the game's own switch for the encounter offline.
            void* charData = Get<void*>(Get<void*>(player, Offset("Actor", "characterDataTracker")), Offset("CharacterDataTracker", "charData"));
            void* features = game::StaticObject(game::FindStaticField("LE.dll", "", "Features", "RandomEncounters"));
            void* feature = features ? Get<void*>(features, Offset("RandomEncountersFeatures", "RageOfMorditasOffline")) : nullptr;
            out << "],\"running\":" << Boolean(running && game::IsAlive(running)) << ",\"cycle\":" << static_cast<int>(Get<uint8_t>(charData, Offset("CharacterData", "<Cycle>k__BackingField", "LE.Data")))
                << ",\"featureOffline\":" << (feature ? Boolean(Get<bool>(feature, Offset("Feature", "enabled"))) : "null");
            void* scene = Invoke(game::FindMethod("LE.dll", "", "SceneList", "GetActiveSceneDetails", 0));
            void* sa[]{scene};
            out << ",\"sceneAllows\":" << (scene ? Boolean(Value<bool>(game::FindMethod("LE.dll", "", "RageOfMorditasLogic", "CanRageOfMorditasSpawnInScene", 1), nullptr, sa)) : "null")
                << ",\"probe\":{\"feature\":[" << featureCalls << ',' << featureTrue << "],\"canSpawn\":[" << canCalls << ',' << canTrue << "],\"spawn\":" << spawnCalls
                << ",\"playerSpawn\":" << playerSpawnCalls << ",\"placed\":" << placeCalls << ",\"zoneFactor\":" << lastZoneFactor.load() << "}}";
            return out.str();
        }
        if (action == "probe") { InstallProbe(); return "{\"ok\":true,\"probe\":true}"; }
        if (action == "curve") {
            // Read only: the hunt's escalation curve (each kill adds its time divided by this, at the hunt's end time).
            if (!running || !game::IsAlive(running)) running = SceneEncounter();
            Capture();
            void* config = running ? Get<void*>(running, Offset("RageOfMorditasLogic", "_encounterTypeConfig")) : nullptr;
            if (!config && !configs.empty()) config = configs.front().object->Get();
            void* curve = config ? Get<void*>(config, Offset("RageOfMorditasEncounterConfig", "HuntTimerEscalationCurve")) : nullptr;
            if (!curve) throw std::runtime_error("Escalation curve unavailable.");
            const auto evaluate = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "AnimationCurve", "Evaluate", 1);
            std::ostringstream out; out.setf(std::ios::fixed); out.precision(4); out << "{\"ok\":true,\"fromEncounter\":" << Boolean(running != nullptr) << ",\"curve\":[";
            for (int t = 0; t <= 200; t += 5) { float x = static_cast<float>(t); void* a[]{&x}; out << (t ? "," : "") << '[' << t << ',' << Value<float>(evaluate, curve, a) << ']'; }
            out << "]}"; return out.str();
        }
        if (action == "goto-crystal") {
            // Disposable test character only: next to the first crystal.
            const auto crystals = Crystals(); if (crystals.empty()) throw std::runtime_error("No crystal in this scene.");
            const auto target = Position(crystals.front());
            void* agent = Get<void*>(player, Offset("Actor", "navMeshAgent"));
            const float offsets[][2]{{3, 0}, {-3, 0}, {0, 3}, {0, -3}, {2, 2}, {-2, -2}, {4, 0}, {0, 4}};
            for (const auto& o : offsets) {
                Vec3 p = target; p.x += o[0]; p.z += o[1]; void* args[]{&p};
                if (Value<bool>(Method(agent, "Warp", 1), agent, args)) return "{\"ok\":true,\"warped\":true}";
            }
            throw std::runtime_error("No place near the crystal.");
        }
        if (action == "start") {
            // Disposable test character only: the crystal is used, as clicking it does.
            if (!running || !game::IsAlive(running)) running = SceneEncounter();
            if (!running) throw std::runtime_error("No Rage of Morditas encounter in this scene.");
            void* sync = Invoke(Method(player, "get_Sync", 0), player);  // the player's PlayerActorSync
            if (!sync) throw std::runtime_error("Player sync unavailable.");
            void* args[]{sync};
            Invoke(Method(running, "Server_OnInteractWithRageOfMorditasStart", 1), running, args);
            return State();
        }
        if (action.rfind("gen-", 0) == 0) {
            // Read only (items are made, never dropped or stored): n reward items the way the encounter
            // makes them, counted by forging potential type (0 default, 1 Blood, 2 Ice) and rarity.
            const auto dash = action.find('-', 4);
            const int n = std::stoi(action.substr(4, dash - 4)), level = dash == std::string::npos ? 90 : std::stoi(action.substr(dash + 1));
            if (n < 1 || n > 2000 || level < 1 || level > 100) throw std::runtime_error("use gen-<1..2000>-<level>");
            Capture();
            void* config = nullptr;
            for (auto& o : configs) { void* c = o.object->Get(); if (c && Get<int>(c, f.minLevel) <= level && level <= Get<int>(c, f.maxLevel)) { config = c; break; } }
            if (!config && !configs.empty()) config = configs.front().object->Get();
            void* generator = Get<void*>(player, Offset("Actor", "generateItems"));
            if (!generator || !config) throw std::runtime_error("Item generation unavailable.");
            const auto* method = Method(generator, "GenerateItemForRageOfMorditas", 6);
            const size_t typeOffset = Offset("ItemData", "forgingPotentialType"), rarityOffset = Offset("ItemData", "rarity");
            std::array<int, 3> types{}; std::array<int, 12> rarities{}; int empty = 0;
            for (int i = 0; i < n; ++i) {
                bool giftable = false, unique = false; int ilvl = level, forced = -1; float subtype = 0;
                void* args[]{&giftable, &ilvl, &forced, &subtype, config, &unique};
                void* item = Invoke(method, generator, args);
                if (!item) { ++empty; continue; }
                const int t = Get<int>(item, typeOffset); if (t >= 0 && t < 3) ++types[t];
                const int r = Get<uint8_t>(item, rarityOffset); if (r >= 0 && r < 12) ++rarities[r];
            }
            std::ostringstream out; out << "{\"ok\":true,\"items\":" << n << ",\"empty\":" << empty << ",\"forgingTypes\":[" << types[0] << ',' << types[1] << ',' << types[2] << "],\"rarities\":[";
            for (int r = 0; r < 12; ++r) out << (r ? "," : "") << rarities[r];
            out << "]}"; return out.str();
        }
        throw std::runtime_error("use ragetest <id> crystals|goto-crystal|start|gen-<n>-<level>");
    });
}
#endif
}  // namespace ep::rage
