#include "player.hpp"

#include "common.hpp"
#include "feature.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "mainthread.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace ep::player {

namespace {

using il2cpp::Method;

// ---- speed: the game's own "increased movement speed" stat (SP.Movespeed = 9), written
// straight into the player's Stats.Stat entry for it. No game function is called: the
// entry's increasedValue field is a plain float, the flag that asks BaseStats to recompute
// is a plain byte, and the whole stat pipeline (character sheet, WalkAnimationScaler's
// animation speed, SpeedManager -> NavMeshAgent speed, click-to-move) then reads the
// boosted value exactly as it reads any other movement speed modifier.
//
// Layouts from the metadata dump: Stats.stats (a List<Stats.Stat>, inherited by BaseStats)
// at +0x88; Stats.Stat.property at +0x10, .specialTag +0x11, .tags +0x14, .extraTag +0x18,
// .addedValue +0x1C, .increasedValue +0x20; BaseStats.statsNeedToBeUpdatedNextFrame +0xD8.

constexpr size_t kStatsListOffset = 0x88;
constexpr size_t kStatProperty = 0x10, kStatSpecialTag = 0x11, kStatTags = 0x14, kStatExtraTag = 0x18;
constexpr size_t kStatAdded = 0x1C;
constexpr size_t kStatIncreased = 0x20;
constexpr size_t kStatsNeedUpdate = 0xD8;
constexpr size_t kListItems = 0x10, kListSize = 0x18;
constexpr size_t kArrayLength = 0x18, kArrayFirst = 0x20;
constexpr int kMaxStats = 512;
constexpr uint8_t kMovespeed = 9;

std::atomic<double> g_speedValue{1.0};    // the multiplier the owner last asked for
std::atomic<double> g_speedPercent{0.0};  // the modifier we have applied, in percent
std::atomic<uint64_t> g_speedApplied{0}, g_speedRefused{0};

size_t g_mutatorOffset = 0;  // Actor.characterMutator (+0x108)
size_t g_myStatsOffset = 0;  // CharacterMutator.myStats (+0x98)
game::MethodRef m_playerActor;

// The found entry and the value it had before we ever touched it (only valid for the
// BaseStats instance it was found on).
void* g_statsPtr = nullptr;
void* g_moveStat = nullptr;
float g_moveStatBase = 0.0f;

void* LocalPlayerActor() {
    if (!m_playerActor) return nullptr;
    void* actor = nullptr;
    game::Guarded([&] { actor = reinterpret_cast<void* (*)(const Method*)>(m_playerActor.code)(m_playerActor.info); }, nullptr);
    return actor;
}

void* PlayerBaseStats() {
    void* actor = LocalPlayerActor();
    if (!actor || !g_mutatorOffset) return nullptr;
    void* mutator = *reinterpret_cast<void* const*>(static_cast<char*>(actor) + g_mutatorOffset);
    if (!mutator || !g_myStatsOffset) return nullptr;
    return *reinterpret_cast<void* const*>(static_cast<char*>(mutator) + g_myStatsOffset);
}

// The (SP.Movespeed, tags 0) entry, or null when the character has none yet. Fills `base`
// with the value the entry has right now.
void* FindMoveStat(void* stats, float* base) {
    void* list = *reinterpret_cast<void* const*>(static_cast<char*>(stats) + kStatsListOffset);
    if (!list) return nullptr;
    void* arr = *reinterpret_cast<void* const*>(static_cast<char*>(list) + kListItems);
    int32_t size = *reinterpret_cast<int32_t*>(static_cast<char*>(list) + kListSize);
    if (!arr || size <= 0 || size > 4096) return nullptr;
    const int32_t length = *reinterpret_cast<int32_t*>(static_cast<char*>(arr) + kArrayLength);
    if (size > length) size = length;
    for (int32_t i = 0; i < size; ++i) {
        void* stat = *reinterpret_cast<void* const*>(static_cast<char*>(arr) + kArrayFirst + 8 * i);
        if (!stat) continue;
        if (*reinterpret_cast<const uint8_t*>(static_cast<const char*>(stat) + kStatProperty) != kMovespeed) continue;
        if (*reinterpret_cast<const uint8_t*>(static_cast<const char*>(stat) + kStatSpecialTag) != 0) continue;
        if (*reinterpret_cast<const int32_t*>(static_cast<const char*>(stat) + kStatTags) != 0) continue;
        if (*reinterpret_cast<const int32_t*>(static_cast<const char*>(stat) + kStatExtraTag) != 0) continue;
        if (base) *base = *reinterpret_cast<const float*>(static_cast<const char*>(stat) + kStatIncreased);
        return stat;
    }
    return nullptr;
}

// Runs on the game's main thread. Writes the wanted percentage into the entry (on top of
// the value it had before we first touched it) and asks BaseStats to recompute.
void ApplySpeedOnMain(double m) {
    void* stats = PlayerBaseStats();
    if (!stats) return;
    if (stats != g_statsPtr) {  // new zone/actor: find the entry and remember its own value
        g_statsPtr = stats;
        g_moveStatBase = 0.0f;
        g_moveStat = FindMoveStat(stats, &g_moveStatBase);
        if (g_moveStat)
            Log("speed: found the Movespeed stat entry (base increasedValue %.1f)", g_moveStatBase);
        else
            Log("speed: the player has no Movespeed stat entry yet");
    }
    if (!g_moveStat) return;
    const double target = m - 1.0;  // the increased field is a fraction: 1.0 = +100%
    *reinterpret_cast<float*>(static_cast<char*>(g_moveStat) + kStatIncreased) = static_cast<float>(g_moveStatBase + target);
    *reinterpret_cast<uint8_t*>(static_cast<char*>(stats) + kStatsNeedUpdate) = 1;
    g_speedPercent = target;
    g_speedApplied.fetch_add(1);
    Log("speed: Movespeed increasedValue %.2f -> %.2f (+%g%%, x%g)", g_moveStatBase, g_moveStatBase + target, target * 100.0, m);
}

// ---- generic stat command: research/stat-map.md's table in code. `increased` true means
// the value is written as a fraction into increasedValue (1.0 = +100%); false writes the
// flat addedValue (what "+75% fire resistance" items use).

struct StatDef {
    const char* name;
    uint8_t sp;
    int32_t tags;
    bool increased;
};

constexpr int32_t AT_Physical = 1, AT_Lightning = 2, AT_Cold = 4, AT_Fire = 8, AT_Void = 16, AT_Necrotic = 32,
                    AT_Poison = 64, AT_Spell = 256, AT_Melee = 512, AT_Throwing = 1024, AT_Bow = 2048, AT_DoT = 4096,
                    AT_Minion = 8192;

const StatDef kStatDefs[] = {
    // base attributes and pools
    {"strength", 19, 0, false},        {"vitality", 20, 0, false},       {"intelligence", 21, 0, false},
    {"dexterity", 22, 0, false},       {"attunement", 23, 0, false},     {"health", 7, 0, false},
    {"mana", 8, 0, false},             {"healthregen", 17, 0, false},    {"manaregen", 18, 0, false},
    {"movespeed", 9, 0, true},
    // resistances
    {"fire", 13, 0, false},            {"cold", 14, 0, false},           {"lightning", 15, 0, false},
    {"void", 26, 0, false},            {"necrotic", 27, 0, false},       {"poison", 28, 0, false},
    {"physical", 64, 0, false},        {"allres", 30, 0, false},
    // defenses
    {"armor", 10, 0, false},           {"dodge", 11, 0, false},          {"stunavoid", 12, 0, false},
    {"wardretention", 16, 0, false},   {"block", 29, 0, false},          {"blockeffect", 53, 0, false},
    {"endurance", 75, 0, false},       {"endurancethreshold", 76, 0, false}, {"critavoid", 89, 0, false},
    {"glancing", 62, 0, false},        {"parry", 121, 0, false},         {"wardregen", 92, 0, false},
    {"warddecay", 119, 0, false},      {"healthleech", 51, 0, false},    {"freezerate", 67, 0, true},
    {"cooldownrecovery", 70, 0, true}, {"increasedleech", 102, 0, true},
    // offense
    {"critchance", 4, 0, true},        {"critmulti", 5, 0, true},        {"castspeed", 3, 0, true},
    {"attackspeed", 2, 0, true},       {"meleeattackspeed", 2, AT_Melee, true}, {"bowattackspeed", 2, AT_Bow, true},
    {"throwingattackspeed", 2, AT_Throwing, true},
    {"damage", 0, 0, true},            {"meleedamage", 0, AT_Melee, true}, {"bowdamage", 0, AT_Bow, true},
    {"spelldamage", 0, AT_Spell, true}, {"throwingdamage", 0, AT_Throwing, true}, {"dotdamage", 0, AT_DoT, true},
    {"firedamage", 0, AT_Fire, true},  {"colddamage", 0, AT_Cold, true}, {"lightningdamage", 0, AT_Lightning, true},
    {"physicaldamage", 0, AT_Physical, true}, {"voiddamage", 0, AT_Void, true}, {"necroticdamage", 0, AT_Necrotic, true},
    {"poisondamage", 0, AT_Poison, true},
    {"firepen", 59, AT_Fire, false},   {"coldpen", 59, AT_Cold, false},  {"lightningpen", 59, AT_Lightning, false},
    {"physicalpen", 59, AT_Physical, false}, {"voidpen", 59, AT_Void, false}, {"necroticpen", 59, AT_Necrotic, false},
    {"poisonpen", 59, AT_Poison, false},
    // minions
    {"miniondamage", 0, AT_Minion, true}, {"minionhealth", 7, AT_Minion, false}, {"minionmovespeed", 9, AT_Minion, true},
    {"minionattackspeed", 2, AT_Minion | AT_Melee, true}, {"minioncastspeed", 3, AT_Minion, true},
    {"minioncritchance", 4, AT_Minion, true}, {"minioncritmulti", 5, AT_Minion, true},
    {"maxcompanions", 61, 0, false},
};

const StatDef* FindStatDef(const std::string& name) {
    for (const StatDef& d : kStatDefs)
        if (name == d.name) return &d;
    return nullptr;
}

// The (sp, tags, no special/extra tag) entry, or null. Fills what it finds.
void* FindStatEntry(void* stats, uint8_t sp, int32_t tags, float* added, float* increased) {
    void* list = *reinterpret_cast<void* const*>(static_cast<char*>(stats) + kStatsListOffset);
    if (!list) return nullptr;
    void* arr = *reinterpret_cast<void* const*>(static_cast<char*>(list) + kListItems);
    int32_t size = *reinterpret_cast<int32_t*>(static_cast<char*>(list) + kListSize);
    if (!arr || size <= 0 || size > 4096) return nullptr;
    const int32_t length = *reinterpret_cast<int32_t*>(static_cast<char*>(arr) + kArrayLength);
    if (size > length) size = length;
    for (int32_t i = 0; i < size; ++i) {
        void* stat = *reinterpret_cast<void* const*>(static_cast<char*>(arr) + kArrayFirst + 8 * i);
        if (!stat) continue;
        if (*reinterpret_cast<const uint8_t*>(static_cast<const char*>(stat) + kStatProperty) != sp) continue;
        if (*reinterpret_cast<const uint8_t*>(static_cast<const char*>(stat) + kStatSpecialTag) != 0) continue;
        if (*reinterpret_cast<const int32_t*>(static_cast<const char*>(stat) + kStatTags) != tags) continue;
        if (*reinterpret_cast<const int32_t*>(static_cast<const char*>(stat) + kStatExtraTag) != 0) continue;
        if (added) *added = *reinterpret_cast<const float*>(static_cast<const char*>(stat) + kStatAdded);
        if (increased) *increased = *reinterpret_cast<const float*>(static_cast<const char*>(stat) + kStatIncreased);
        return stat;
    }
    return nullptr;
}

// Finds the entry or creates one with il2cpp_object_new and the list's own Add method.
void* EnsureStatEntry(void* stats, const StatDef& def, bool* created) {
    if (void* found = FindStatEntry(stats, def.sp, def.tags, nullptr, nullptr)) return found;
    void* list = *reinterpret_cast<void* const*>(static_cast<char*>(stats) + kStatsListOffset);
    if (!list) {
        Log("stat: create SP=%u tags=%d: the stats list is null", static_cast<unsigned>(def.sp), def.tags);
        return nullptr;
    }
    // The class of any existing entry is the exact instantiated Stats.Stat class.
    const il2cpp::Class* statClass = nullptr;
    void* arr = *reinterpret_cast<void* const*>(static_cast<char*>(list) + kListItems);
    const int32_t size = *reinterpret_cast<int32_t*>(static_cast<char*>(list) + kListSize);
    if (arr && size > 0) {
        if (void* first = *reinterpret_cast<void* const*>(static_cast<char*>(arr) + kArrayFirst))
            statClass = il2cpp::api().object_get_class(first);
    }
    if (!statClass) statClass = game::FindClass("LE.dll", "", "Stats.Stat");
    if (!statClass) {
        Log("stat: create SP=%u tags=%d: no Stats.Stat class", static_cast<unsigned>(def.sp), def.tags);
        return nullptr;
    }
    void* obj = il2cpp::api().object_new(statClass);
    if (!obj) {
        Log("stat: create SP=%u: object_new returned null", static_cast<unsigned>(def.sp));
        return nullptr;
    }
    *reinterpret_cast<uint8_t*>(static_cast<char*>(obj) + kStatProperty) = def.sp;
    *reinterpret_cast<int32_t*>(static_cast<char*>(obj) + kStatTags) = def.tags;
    const il2cpp::Class* listClass = il2cpp::api().object_get_class(list);
    const il2cpp::Method* add = listClass ? il2cpp::api().class_get_method_from_name(listClass, "Add", 1) : nullptr;
    Log("stat: create SP=%u tags=%d: class %p, object %p, listClass %p, Add %p", static_cast<unsigned>(def.sp), def.tags,
        static_cast<const void*>(statClass), obj, static_cast<const void*>(listClass), static_cast<const void*>(add));
    if (!add) return nullptr;
    void* addCode = *reinterpret_cast<void* const*>(add);
    reinterpret_cast<void (*)(void*, void*, const il2cpp::Method*)>(addCode)(list, obj, add);
    if (created) *created = true;
    return obj;
}

// ---- cooldown: PlayerChargeManager.OnUpdateTick(float deltaTime) drives the player's
// charges and cooldowns once a frame. Scaling deltaTime scales that countdown only (the
// manager exists on the player, not on monsters). ChargeManager.getCooldown stays hooked as
// the length source for the abilities that ask for it.

feature::Feature g_cooldown;

using ChargeTickFn = void (*)(void* self, float deltaTime, const Method* m);
ChargeTickFn o_chargeTick = nullptr;

void d_chargeTick(void* self, float deltaTime, const Method* m) {
    double mult = 1.0;
    if (deltaTime > 0.0f && feature::Active(g_cooldown, &mult)) {
        const float scaled = static_cast<float>(deltaTime * mult);
        if (g_cooldown.firstPending.exchange(false)) Log("cooldown: first boosted tick dt %g -> %g (x%g)", deltaTime, scaled, mult);
        deltaTime = scaled;
    }
    o_chargeTick(self, deltaTime, m);
}

feature::Hook h_chargeTick{"PlayerChargeManager.OnUpdateTick", {}, reinterpret_cast<void*>(&d_chargeTick),
                           reinterpret_cast<void**>(&o_chargeTick)};

using GetCooldownFn = float (*)(void* self, int32_t index, const Method* m);
GetCooldownFn o_getCooldown = nullptr;

float d_getCooldown(void* self, int32_t index, const Method* m) {
    const float seconds = o_getCooldown(self, index, m);
    double mult = 1.0;
    if (seconds > 0.0f && feature::Active(g_cooldown, &mult)) {
        float scaled = static_cast<float>(seconds / mult);
        if (scaled < 0.05f) scaled = 0.05f;
        if (g_cooldown.firstPending.exchange(false))
            Log("cooldown: first boosted cooldown %g -> %g (x%g)", seconds, scaled, mult);
        return scaled;
    }
    return seconds;
}

feature::Hook h_getCooldown{"ChargeManager.getCooldown", {}, reinterpret_cast<void*>(&d_getCooldown),
                            reinterpret_cast<void**>(&o_getCooldown)};

}  // namespace

bool Init() {
    g_cooldown.cmd = "cooldown";
    g_cooldown.max = 10;
    g_cooldown.hooks = {&h_chargeTick, &h_getCooldown};

    m_playerActor = game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0);
    g_mutatorOffset = game::FieldOffset("LE.dll", "", "Actor", "characterMutator");
    g_myStatsOffset = game::FieldOffset("LE.dll", "", "CharacterMutator", "myStats");
    h_chargeTick.ref = game::FindMethod("LE.dll", "", "PlayerChargeManager", "OnUpdateTick", 1);
    h_getCooldown.ref = game::FindMethod("LE.dll", "", "ChargeManager", "getCooldown", 1);

    Log("player: PlayerFinder.getPlayerActor %s, Actor.characterMutator +0x%zX, CharacterMutator.myStats +0x%zX; PlayerChargeManager.OnUpdateTick "
        "%s, ChargeManager.getCooldown %s",
        m_playerActor ? "found" : "MISSING", g_mutatorOffset, g_myStatsOffset, h_chargeTick.ref ? "found" : "MISSING",
        h_getCooldown.ref ? "found" : "MISSING");
    return static_cast<bool>(m_playerActor) && static_cast<bool>(h_chargeTick.ref);
}

std::string SetSpeed(double m) {
    char buf[220];
    if (!m_playerActor || !g_mutatorOffset || !g_myStatsOffset)
        return "speed: refused: the movement stat path was not found in this game build";
    if (!(m >= 1.0 && m <= kMaxSpeed)) {
        std::snprintf(buf, sizeof buf, "speed: refused: the multiplier must be between 1 and %g", kMaxSpeed);
        return buf;
    }
    if (m > 1.0 && !game::IsOfflinePlay()) {
        g_speedRefused.fetch_add(1);
        return "speed: refused: " + game::GateText();
    }
    // Pure managed-memory reads/writes (plus the list's Add for a missing entry): safe from
    // the attached command thread, so these keep working even when the game window is not
    // focused and the main thread is not ticking.
    game::Guarded([&] { ApplySpeedOnMain(m); }, nullptr);
    const double target = m - 1.0;
    if (std::fabs(g_speedPercent.load() - target) > 0.001)
        return "speed: refused: the player has no Movespeed stat entry yet (equip an item with movement speed or gain any movement buff once, "
               "then retry)";
    g_speedValue = m;
    if (m == 1.0) return "speed -> x1 (Movespeed modifier removed)";
    std::snprintf(buf, sizeof buf, "speed -> x%g (+%.0f%% increased movement speed, the game's own stat)", m, target * 100.0);
    return buf;
}

std::string SetCooldown(double m) { return feature::Set(g_cooldown, m); }

namespace {

std::string ReadStatImpl(const std::string& name) {
    const StatDef* def = FindStatDef(name);
    if (!def) return "stat: unknown name (run `stat` without arguments for the list)";
    void* stats = PlayerBaseStats();
    if (!stats) return "stat: no player stats (enter a zone first)";
    float added = 0.0f, increased = 0.0f;
    void* entry = FindStatEntry(stats, def->sp, def->tags, &added, &increased);
    char buf[220];
    if (!entry) {
        std::snprintf(buf, sizeof buf, "stat %s: no entry yet (SP=%u tags=%d); `stat %s <value>` creates one", name.c_str(),
                      static_cast<unsigned>(def->sp), def->tags, name.c_str());
        return buf;
    }
    std::snprintf(buf, sizeof buf, "stat %s: added %.2f, increased %.2f (+%.0f%%) (SP=%u tags=%d)", name.c_str(), added, increased,
                  increased * 100.0, static_cast<unsigned>(def->sp), def->tags);
    return buf;
}

std::string SetStatImpl(const std::string& name, double value) {
    const StatDef* def = FindStatDef(name);
    if (!def) return "stat: unknown name (run `stat` without arguments for the list)";
    void* stats = PlayerBaseStats();
    if (!stats) return "stat: no player stats (enter a zone first)";
    bool created = false;
    void* entry = EnsureStatEntry(stats, *def, &created);
    if (!entry) return "stat: could not find or create the entry";
    const float v = static_cast<float>(value);
    if (def->increased)
        *reinterpret_cast<float*>(static_cast<char*>(entry) + kStatIncreased) = v;
    else
        *reinterpret_cast<float*>(static_cast<char*>(entry) + kStatAdded) = v;
    *reinterpret_cast<uint8_t*>(static_cast<char*>(stats) + kStatsNeedUpdate) = 1;
    char buf[220];
    std::snprintf(buf, sizeof buf, "stat %s -> %s %.2f (SP=%u tags=%d)%s", name.c_str(), def->increased ? "increased" : "added", v,
                  static_cast<unsigned>(def->sp), def->tags, created ? " [new entry created]" : "");
    return buf;
}

}  // namespace

std::string StatList() {
    std::string out = "stat names:";
    for (const StatDef& d : kStatDefs) {
        out += ' ';
        out += d.name;
    }
    out += "\nusage: stat <name> [value]   (fire 75 -> +75 flat; bowattackspeed 3 -> +300% increased)";
    return out;
}

std::string ReadStat(const std::string& name) {
    std::string reply;
    game::Guarded([&] { reply = ReadStatImpl(name); }, nullptr);
    return reply.empty() ? std::string("stat: read failed (guarded)") : reply;
}

std::string SetStat(const std::string& name, double value) {
    std::string reply;
    game::Guarded([&] { reply = SetStatImpl(name, value); }, nullptr);
    return reply.empty() ? std::string("stat: write failed (guarded)") : reply;
}

namespace {

std::string StatScanImpl(int sp) {
    void* stats = PlayerBaseStats();
    if (!stats) return "statscan: no player stats (enter a zone first)";
    char head[96];
    std::snprintf(head, sizeof head, "statscan SP=%d:", sp);
    std::string out = head;
    void* list = *reinterpret_cast<void* const*>(static_cast<char*>(stats) + kStatsListOffset);
    if (!list) return out + " the stats list is null";
    void* arr = *reinterpret_cast<void* const*>(static_cast<char*>(list) + kListItems);
    int32_t size = *reinterpret_cast<int32_t*>(static_cast<char*>(list) + kListSize);
    const int32_t length = arr ? *reinterpret_cast<int32_t*>(static_cast<char*>(arr) + kArrayLength) : -1;
    char counts[64];
    std::snprintf(counts, sizeof counts, " %d entries (of %d):", size, length);
    out += counts;
    if (!arr || size <= 0) return out;
    if (size > length) size = length;
    if (size > 256) size = 256;
    for (int32_t i = 0; i < size; ++i) {
        void* stat = *reinterpret_cast<void* const*>(static_cast<char*>(arr) + kArrayFirst + 8 * i);
        if (!stat) continue;
        const int prop = *reinterpret_cast<const uint8_t*>(static_cast<const char*>(stat) + kStatProperty);
        if (prop != sp) continue;
        char line[160];
        std::snprintf(line, sizeof line, " tags=%d special=%u extra=%d add=%.2f inc=%.3f;", 
                      *reinterpret_cast<const int32_t*>(static_cast<const char*>(stat) + kStatTags),
                      static_cast<unsigned>(*reinterpret_cast<const uint8_t*>(static_cast<const char*>(stat) + kStatSpecialTag)),
                      *reinterpret_cast<const int32_t*>(static_cast<const char*>(stat) + kStatExtraTag),
                      *reinterpret_cast<const float*>(static_cast<const char*>(stat) + kStatAdded),
                      *reinterpret_cast<const float*>(static_cast<const char*>(stat) + kStatIncreased));
        out += line;
    }
    return out;
}

}  // namespace

std::string StatScan(int sp) {
    std::string reply;
    game::Guarded([&] { reply = StatScanImpl(sp); }, nullptr);
    return reply.empty() ? std::string("statscan: read failed (guarded)") : reply;
}

std::string SpeedProbe() {
    char buf[128];
    std::snprintf(buf, sizeof buf, "speed modifier %+.0f%% (x%g)", g_speedPercent.load() * 100.0, g_speedValue.load());
    return buf;
}

// For the live check: every entry in the player's stat list that mentions Movespeed, and
// the size of the list. Main thread only.
std::string StatProbe() {
    void* stats = PlayerBaseStats();
    if (!stats) return "statprobe: no player BaseStats (in a zone?)";
    void* list = *reinterpret_cast<void* const*>(static_cast<char*>(stats) + kStatsListOffset);
    if (!list) return "statprobe: the stats list is null";
    void* arr = *reinterpret_cast<void* const*>(static_cast<char*>(list) + kListItems);
    int32_t size = *reinterpret_cast<int32_t*>(static_cast<char*>(list) + kListSize);
    const int32_t length = arr ? *reinterpret_cast<int32_t*>(static_cast<char*>(arr) + kArrayLength) : -1;
    char head[128];
    std::snprintf(head, sizeof head, "statprobe: list %p size %d length %d;", list, size, length);
    std::string out = head;
    if (!arr || size <= 0) return out;
    if (size > length) size = length;
    if (size > 64) size = 64;
    for (int32_t i = 0; i < size; ++i) {
        void* stat = *reinterpret_cast<void* const*>(static_cast<char*>(arr) + kArrayFirst + 8 * i);
        if (!stat) continue;
        const uint8_t sp = *reinterpret_cast<const uint8_t*>(static_cast<const char*>(stat) + kStatProperty);
        const int32_t tags = *reinterpret_cast<const int32_t*>(static_cast<const char*>(stat) + kStatTags);
        const float added = *reinterpret_cast<const float*>(static_cast<const char*>(stat) + 0x1c);
        const float increased = *reinterpret_cast<const float*>(static_cast<const char*>(stat) + kStatIncreased);
        if (sp != kMovespeed) continue;
        char line[128];
        std::snprintf(line, sizeof line, " SP=%u tags=%d added=%.1f increased=%.1f;", sp, tags, added, increased);
        out += line;
    }
    return out;
}

std::string Status() {
    char buf[240];
    std::snprintf(buf, sizeof buf, "speed: x%g, modifier %+.1f%%, applied %llu, refused online %llu", g_speedValue.load(),
                  g_speedPercent.load(), static_cast<unsigned long long>(g_speedApplied.load()),
                  static_cast<unsigned long long>(g_speedRefused.load()));
    return std::string(buf) + "\n" + feature::Line(g_cooldown);
}

}  // namespace ep::player
