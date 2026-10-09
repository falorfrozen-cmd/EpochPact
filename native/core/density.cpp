#include "density.hpp"
#include "density_rules.hpp"
#include "common.hpp"
#include "feature.hpp"
#include "game.hpp"
#include "mainthread.hpp"
#include <array>
#include <map>
#include <sstream>
#include <stdexcept>

namespace ep::density {
namespace {
using il2cpp::Method;
using game::MethodRef;
feature::Feature feature;
struct Layout {
    size_t mean = 0, variance = 0, data = 0, friendly = 0, champion = 0, context = 0;
    size_t type = 0, loneBoss = 0, dataChampion = 0, omen = 0, nemesis = 0, harbinger = 0;
    size_t generator = 0, spawned = 0, total = 0;
} o;
int normalType = -1, defaultContext = -1, waveContext = -1;
const il2cpp::Class* spawnerClass = nullptr;
MethodRef findObjects, queueCount;
bool ready = false;
std::array<std::atomic<uint64_t>, 8> skipped{};
std::atomic<uint64_t> generated{0}, planned{0}, spawned{0}, completed{0}, limited{0}, evicted{0}, faults{0};
std::atomic<int> lastPlanned{0}, lastSpawned{0};
constexpr size_t MaxTracked = 256;
struct Tracked { game::Handle source; uint64_t serial = 0; int initial = 0, observed = 0, planned = 0; };
struct Context {
    std::map<void*, Tracked> tracked;
    std::map<const il2cpp::Class*, size_t> countOffsets;
    uint64_t serial = 0;
};
// Do not call IL2CPP while DLL CRT globals are being destroyed after Unity shutdown.
Context& state = *new Context;
template<class T> T& At(void* p, size_t offset) { return *reinterpret_cast<T*>(static_cast<char*>(p) + offset); }

void* Invoke(MethodRef method, void* self, void** args = nullptr) {
    void* exception = nullptr;
    if (!method.info) throw std::runtime_error("required method missing");
    void* value = il2cpp::api().runtime_invoke(method.info, self, args, &exception);
    if (exception) throw std::runtime_error("managed exception in density snapshot");
    return value;
}
template<class F> std::string OnMainThread(const char* command, F&& work) {
    std::string result, why;
    const bool ran = mainthread::Run([&] {
        std::string failure;
        if (!game::Guarded([&] { result = work(); }, &failure)) result = std::string(command) + ": refused: " + failure;
    }, 5000, &why);
    return ran ? result : std::string(command) + ": refused: " + why;
}
int ListCount(void* list) {
    if (!list) return 0;
    const auto* cls = il2cpp::api().object_get_class(list);
    auto it = state.countOffsets.find(cls);
    if (it == state.countOffsets.end()) {
        const auto* field = il2cpp::api().class_get_field_from_name(cls, "_size");
        if (!field) throw std::runtime_error("List._size missing");
        it = state.countOffsets.emplace(cls, il2cpp::api().field_get_offset(field)).first;
    }
    const int count = At<int>(list, it->second);
    if (count < 0 || count > 100000) throw std::runtime_error("invalid spawn list count");
    return count;
}
rules::Pack Pack(void* self) {
    rules::Pack p;
    p.mean = At<float>(self, o.mean); p.variance = At<float>(self, o.variance);
    void* data = At<void*>(self, o.data);
    if (!data) return p;
    p.known = true;
    p.normal = At<int>(data, o.type) == normalType;
    p.special = At<bool>(self, o.champion) || At<bool>(data, o.loneBoss) || At<bool>(data, o.dataChampion) ||
                At<bool>(data, o.omen) || At<bool>(data, o.nemesis) || At<bool>(data, o.harbinger);
    p.friendly = At<bool>(self, o.friendly);
    const int context = At<int>(self, o.context);
    p.summoned = context != defaultContext && context != waveContext;
    return p;
}
void Erase(std::map<void*, Tracked>::iterator it) { it->second.source.Reset(); state.tracked.erase(it); }
void Clear() { while (!state.tracked.empty()) Erase(state.tracked.begin()); }
void Observe(void* self, Tracked& t) {
    const int count = ListCount(At<void*>(self, o.spawned));
    const int delta = count - t.initial - t.observed;
    if (delta > 0) { t.observed += delta; spawned.fetch_add(delta); }
}
void Track(void* self, int initial, int count) {
    for (auto it = state.tracked.begin(); it != state.tracked.end();) {
        if (!it->second.source.Get() || it->first == self) { auto old = it++; Erase(old); }
        else ++it;
    }
    if (state.tracked.size() == MaxTracked) {
        auto oldest = state.tracked.begin();
        for (auto it = state.tracked.begin(); it != state.tracked.end(); ++it)
            if (it->second.serial < oldest->second.serial) oldest = it;
        Erase(oldest); ++evicted;
    }
    auto& t = state.tracked[self];
    t.source.Set(self); t.serial = ++state.serial; t.initial = initial; t.planned = count;
    Observe(self, t);
}

using GenerateFn = void (*)(void*, const Method*);
using SpawnOneFn = bool (*)(void*, const Method*);
GenerateFn originalGenerate = nullptr, originalFinish = nullptr;
SpawnOneFn originalSpawnOne = nullptr;

void Generate(void* self, const Method* method) {
    if (!self || !ready || rules::Scope::Contains(self)) { originalGenerate(self, method); return; }
    rules::Scope scope(self);
    double multiplier = feature.value.load();
    rules::Plan plan{rules::Reason::Invalid, 0};
    int initial = 0;
    void* previousGenerator = nullptr;
    if (multiplier == 1) { originalGenerate(self, method); return; }
    if (!game::Guarded([&] {
        const bool offline = game::IsOfflinePlay();
        plan = rules::Evaluate(Pack(self), multiplier, offline);
        initial = ListCount(At<void*>(self, o.spawned));
        previousGenerator = At<void*>(self, o.generator);
    }, nullptr)) { ++faults; originalGenerate(self, method); return; }
    if (plan.reason != rules::Reason::Eligible) {
        ++skipped[static_cast<size_t>(plan.reason)];
        if (plan.reason == rules::Reason::Offline) ++feature.refused;
        originalGenerate(self, method); return;
    }
    // Check again immediately before writing: an offline flag change fails closed.
    if (!game::IsOfflinePlay()) { ++feature.refused; originalGenerate(self, method); return; }
    const float saved = At<float>(self, o.mean);
    ++feature.boosted;
    if (plan.limited) ++limited;
    rules::WithCount(At<float>(self, o.mean), plan.mean, [&] { originalGenerate(self, method); });
    if (!game::Guarded([&] {
        void* generator = At<void*>(self, o.generator);
        if (!generator || generator == previousGenerator) return; // e.g. neverSpawnChance won
        const int count = At<int>(generator, o.total);
        if (count <= 0 || count > 100000) return;
        ++generated; planned.fetch_add(count); lastPlanned = count;
        Track(self, initial, count);
        if (feature.firstPending.exchange(false))
            Log("density: first generated pack: mean %g -> %g (x%g), actual generator queue %d; original mean restored %g",
                saved, plan.mean, multiplier, count, At<float>(self, o.mean));
    }, nullptr)) ++faults;
}
bool SpawnOne(void* self, const Method* method) {
    const bool result = originalSpawnOne(self, method);
    if (!game::Guarded([&] {
        const auto it = state.tracked.find(self);
        if (it != state.tracked.end()) Observe(self, it->second);
    }, nullptr)) ++faults;
    return result; // game result is queue completion, NOT one successful spawn
}
void Finish(void* self, const Method* method) {
    uint64_t serial = 0;
    int count = 0, wanted = 0;
    if (!game::Guarded([&] {
        const auto it = state.tracked.find(self);
        if (it == state.tracked.end()) return;
        auto& t = it->second; Observe(self, t);
        serial = t.serial; count = t.observed; wanted = t.planned;
    }, nullptr)) ++faults;
    originalFinish(self, method);
    if (!serial) return;
    ++completed; lastSpawned = count;
    if (completed.load() <= 3) Log("density: completed pack: spawned %d / planned %d", count, wanted);
    if (!game::Guarded([&] {
        const auto it = state.tracked.find(self);
        if (it != state.tracked.end() && it->second.serial == serial) Erase(it);
    }, nullptr)) ++faults;
}
feature::Hook generateHook{"Spawner.GenerateEntitiesInternal", {}, reinterpret_cast<void*>(&Generate), reinterpret_cast<void**>(&originalGenerate)};
feature::Hook spawnHook{"Spawner.SpawnOneImmediately", {}, reinterpret_cast<void*>(&SpawnOne), reinterpret_cast<void**>(&originalSpawnOne)};
feature::Hook finishHook{"Spawner.FinishSpawning", {}, reinterpret_cast<void*>(&Finish), reinterpret_cast<void**>(&originalFinish)};

int Enum(const char* cls, const char* name) {
    const auto* field = game::FindStaticField("LE.dll", "", cls, name);
    if (!field) return -1;
    int value = -1;
    game::Guarded([&] { il2cpp::api().field_static_get_value(field, &value); }, nullptr);
    return value;
}
} // namespace

bool Init() {
    feature.cmd = "density"; feature.max = rules::MaxMultiplier;
    feature.hooks = {&generateHook, &spawnHook, &finishHook};
    generateHook.ref = game::FindMethod("LE.dll", "", "Spawner", "GenerateEntitiesInternal", 0);
    spawnHook.ref = game::FindMethod("LE.dll", "", "Spawner", "SpawnOneImmediately", 0);
    finishHook.ref = game::FindMethod("LE.dll", "", "Spawner", "FinishSpawning", 0);
    auto field = [](const char* cls, const char* name) { return game::FieldOffset("LE.dll", "", cls, name); };
    o.mean = field("Spawner", "numberToSpawn"); o.variance = field("Spawner", "percentVariance");
    o.data = field("Spawner", "_actorData"); o.friendly = field("Spawner", "forceGood");
    o.champion = field("Spawner", "isChampion"); o.context = field("Spawner", "spawnerContext");
    o.generator = field("Spawner", "monsterGenerator"); o.spawned = field("Spawner", "spawnedActors");
    o.total = field("MonsterGenerator", "totalSpawnCount");
    o.type = field("ActorData", "actorType"); o.loneBoss = field("ActorData", "IsLoneBoss");
    o.dataChampion = field("ActorData", "isChampion"); o.omen = field("ActorData", "isOmen");
    o.nemesis = field("ActorData", "isNemesis"); o.harbinger = field("ActorData", "isHarbinger");
    normalType = Enum("ActorData.Type", "Normal");
    defaultContext = Enum("SpawnerContext", "Default"); waveContext = Enum("SpawnerContext", "WaveSpawner");
    ready = o.mean && o.variance && o.data && o.friendly && o.champion && o.context && o.generator && o.spawned &&
            o.total && o.type && o.loneBoss && o.dataChampion && o.omen && o.nemesis && o.harbinger &&
            normalType >= 0 && defaultContext >= 0 && waveContext >= 0;
    if (!ready) generateHook.ref = {};
    spawnerClass = game::FindClass("LE.dll", "", "Spawner");
    queueCount = game::FindMethod("LE.dll", "", "Spawner", "GetEntityCountInSpawnQueue", 0);
    if (const auto* cls = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object")) {
        void* iter = nullptr;
        const auto& a = il2cpp::api();
        while (const auto* m = a.class_get_methods(cls, &iter)) {
            if (std::string(a.method_get_name(m)) != "FindObjectsOfType" || a.method_get_param_count(m) != 2) continue;
            char* name = a.type_get_name(a.method_get_param(m, 0));
            const bool typed = name && std::string(name) == "System.Type"; if (name) a.free(name);
            name = a.type_get_name(a.method_get_param(m, 1));
            const bool boolean = name && std::string(name) == "System.Boolean"; if (name) a.free(name);
            if (typed && boolean) { findObjects = {m, *reinterpret_cast<void* const*>(m)}; break; }
        }
    }
    Log("density: runtime layout %s; generation/spawn/finish hooks %s/%s/%s; max pre-rarity roll %g",
        ready ? "ready" : "MISSING", generateHook.ref ? "found" : "MISSING", spawnHook.ref ? "found" : "MISSING",
        finishHook.ref ? "found" : "MISSING", rules::MaxRolledCount);
    return ready && generateHook.ref && spawnHook.ref && finishHook.ref;
}
std::string Set(double multiplier) {
    if (!ready) return "density: refused: spawn layout/exclusion metadata is missing";
    return OnMainThread("density", [&] {
        std::string result = feature::Set(feature, multiplier);
        if (multiplier == 1 && feature.value.load() == 1) Clear();
        if (multiplier > 1 && result.find("refused") == std::string::npos)
            result += " (future regular enemy packs; enter/reload a combat zone for full coverage)";
        return result;
    });
}
std::string Status() {
    std::ostringstream out;
    out << feature::Line(feature) << "; generated packs " << generated.load() << ", planned actors " << planned.load()
        << ", observed spawns " << spawned.load() << ", completed packs " << completed.load()
        << ", last completed " << lastSpawned.load() << ", last planned " << lastPlanned.load()
        << ", capped " << limited.load() << ", tracking evictions " << evicted.load() << ", read faults " << faults.load()
        << "; skipped single " << skipped[1].load() << ", special " << skipped[2].load()
        << ", unknown " << skipped[3].load() << ", invalid " << skipped[4].load()
        << ", summons " << skipped[5].load() << ", oversized " << skipped[7].load();
    return out.str();
}
std::string Read() {
    if (!ready || !findObjects || !queueCount || !spawnerClass) return "densityread: refused: snapshot metadata missing";
    return OnMainThread("densityread", [&] {
        if (!game::IsOfflinePlay()) return "densityread: refused: " + game::GateText();
        const auto& a = il2cpp::api();
        void* type = a.type_get_object(a.class_get_type(spawnerClass));
        bool inactive = true; void* args[]{type, &inactive};
        void* array = Invoke(findObjects, nullptr, args);
        if (!array) throw std::runtime_error("spawner lookup returned null");
        const uintptr_t root = a.gchandle_new(array, false);
        struct Free { uintptr_t handle; ~Free() { if (handle) il2cpp::api().gchandle_free(handle); } } free{root};
        if (!root) throw std::runtime_error("spawner snapshot root failed");
        const uintptr_t count = At<uintptr_t>(array, 0x18);
        if (count > 16384) throw std::runtime_error("invalid spawner array count");
        std::array<uint64_t, 8> reasons{};
        uint64_t queues = 0, actors = 0, generatedHere = 0;
        std::ostringstream lines;
        int shown = 0;
        for (uintptr_t i = 0; i < count; ++i) {
            void* self = At<void*>(array, 0x20 + sizeof(void*) * i);
            if (!self || !game::IsAlive(self)) continue;
            const auto pack = Pack(self);
            const auto plan = rules::Evaluate(pack, 2, true); // eligibility even while x1 is off
            ++reasons[static_cast<size_t>(plan.reason)];
            void* value = Invoke(queueCount, self);
            if (!value) throw std::runtime_error("queue count was not boxed");
            const int queue = *static_cast<int*>(a.object_unbox(value));
            const int live = ListCount(At<void*>(self, o.spawned));
            if (queue < 0 || queue > 100000) throw std::runtime_error("invalid spawn queue count");
            queues += queue; actors += live;
            void* generator = At<void*>(self, o.generator);
            const int total = generator ? At<int>(generator, o.total) : 0;
            if (generator) ++generatedHere;
            if (plan.reason == rules::Reason::Eligible && shown++ < 12)
                lines << "\npack " << i << ": original mean=" << pack.mean << " variance=" << pack.variance
                      << " generator total=" << total << " queue=" << queue << " spawned list=" << live;
        }
        std::ostringstream out;
        out << "densityread: scene spawners=" << count << " eligible regular packs=" << reasons[0]
            << " single=" << reasons[1] << " special=" << reasons[2] << " unknown=" << reasons[3]
            << " invalid=" << reasons[4] << " summons=" << reasons[5] << " oversized=" << reasons[7]
            << "; generators=" << generatedHere << " queued actors=" << queues << " spawned list entries=" << actors
            << "\n" << Status() << lines.str()
            << "\nSnapshot includes inactive scene spawners. Spawned list entries can include dead actors; this is not a living-enemy count.";
        return out.str();
    });
}
} // namespace ep::density
