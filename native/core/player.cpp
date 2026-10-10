#include "player.hpp"

#include "common.hpp"
#include "feature.hpp"
#include "game.hpp"
#include "hook.hpp"
#include "mainthread.hpp"
#include "stat_editor.hpp"
#include "cooldown_rules.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace ep::player {

namespace {

using il2cpp::Method;

// Speed uses a separate EpochPact Movespeed contribution through stat_editor.
// The offsets below remain for the read-only movement/stat diagnostics.

size_t kStatsListOffset = 0;
size_t kStatProperty = 0, kStatSpecialTag = 0, kStatTags = 0, kStatExtraTag = 0;
size_t kStatAdded = 0, kStatIncreased = 0, kStatMoreValues = 0;
size_t kStatsNeedUpdate = 0;
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
game::MethodRef m_statCtor, m_updateStats;
game::MethodRef m_gameObject, m_displayChildren;
const il2cpp::Class* g_displayClass = nullptr;
size_t g_displayProperty = 0, g_displayTags = 0, g_displayText = 0, g_displayType = 0;
const il2cpp::Class* g_statClass = nullptr;
const il2cpp::Field* g_sheetInstance = nullptr;
size_t g_sheetStats = 0, g_textOffset = 0;
const char* kResistanceFields[] = {"PhysicalRes", "LightningRes", "ColdRes", "FireRes", "VoidRes", "NecroticRes", "PoisonRes"};
size_t g_resistanceOffsets[7] = {};
bool g_layoutReady = false;

std::string LabelText(void* label) {
    if (!label || !g_textOffset) return "<null>";
    void* text = *reinterpret_cast<void**>(static_cast<char*>(label) + g_textOffset);
    if (!text) return "<null>";
    const int length = *reinterpret_cast<int*>(static_cast<char*>(text) + 0x10);
    if (length < 0 || length > 4096) return "<invalid>";
    const wchar_t* chars = reinterpret_cast<const wchar_t*>(static_cast<char*>(text) + 0x14);
    const int n = WideCharToMultiByte(CP_UTF8, 0, chars, length, nullptr, 0, nullptr, nullptr);
    std::string utf8(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, chars, length, utf8.data(), n, nullptr, nullptr);
    return utf8;
}

std::string OnMain(std::function<std::string()> fn) {
    std::string reply, why;
    if (!mainthread::Run([&] { reply = fn(); }, 3000, &why)) return "stat: refused: " + why;
    return reply.empty() ? "stat: operation failed (guarded; see core.log)" : reply;
}

void Recalculate(void* stats) {
    *reinterpret_cast<uint8_t*>(static_cast<char*>(stats) + kStatsNeedUpdate) = 1;
    // This runs the game's calculation and its afterStatsUpdatedEvent, including
    // CharacterSheet.UpdateSheet. Never call Unity/UI methods from the IPC thread.
    reinterpret_cast<void (*)(void*, const Method*)>(m_updateStats.code)(stats, m_updateStats.info);
}

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

// ---- generic stat command: research/stat-map.md's table in code. `increased` true means
// the value is written as a fraction into increasedValue (1.0 = +100%); false writes the
// flat addedValue (percentage stats use fractions too: fire 0.75 means 75%).

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
    {"reflect", 86, 0, false},        {"damagereflected", 86, 0, false},
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

// ---- cooldown: scale the local player countdown once. Never shorten the
// global ChargeManager.getCooldown length (also used by AI).
feature::Feature g_cooldown;
size_t g_chargeOwner = 0, g_actorCharges = 0;

using ChargeTickFn = void (*)(void* self, float deltaTime, const Method* m);
ChargeTickFn o_chargeTick = nullptr;

void d_chargeTick(void* self, float deltaTime, const Method* m) {
    double mult = 1.0;
    if (deltaTime > 0.0f && feature::Active(g_cooldown, &mult)) {
        bool localOwner = false;
        game::Guarded([&] {
            void* actor = LocalPlayerActor();
            localOwner = actor && self && g_chargeOwner && g_actorCharges &&
                *reinterpret_cast<void**>(static_cast<char*>(self) + g_chargeOwner) == actor &&
                *reinterpret_cast<void**>(static_cast<char*>(actor) + g_actorCharges) == self;
        }, nullptr);
        const float scaled = rules::CooldownDelta(deltaTime, mult, localOwner);
        if (localOwner && g_cooldown.firstPending.exchange(false))
            Log("cooldown: local player countdown dt %g -> %g (x%g)", deltaTime, scaled, mult);
        deltaTime = scaled;
    }
    o_chargeTick(self, deltaTime, m);
}

feature::Hook h_chargeTick{"PlayerChargeManager.OnUpdateTick", {}, reinterpret_cast<void*>(&d_chargeTick),
                           reinterpret_cast<void**>(&o_chargeTick)};

}  // namespace

bool Init() {
    statedit::Init();
    g_cooldown.cmd = "cooldown";
    g_cooldown.max = 10;
    g_cooldown.hooks = {&h_chargeTick};

    m_playerActor = game::FindMethod("LE.dll", "", "PlayerFinder", "getPlayerActor", 0);
    g_mutatorOffset = game::FieldOffset("LE.dll", "", "Actor", "characterMutator");
    g_myStatsOffset = game::FieldOffset("LE.dll", "", "CharacterMutator", "myStats");
    g_statClass = game::FindClass("LE.dll", "", "Stats.Stat");
    m_statCtor = game::FindMethod("LE.dll", "", "Stats.Stat", ".ctor", 0);
    m_updateStats = game::FindMethod("LE.dll", "", "BaseStats", "UpdateStatsInternal", 0);
    kStatsListOffset = game::FieldOffset("LE.dll", "", "Stats", "stats");
    kStatsNeedUpdate = game::FieldOffset("LE.dll", "", "BaseStats", "statsNeedToBeUpdatedNextFrame");
    kStatProperty = game::FieldOffset("LE.dll", "", "Stats.Stat", "property");
    kStatSpecialTag = game::FieldOffset("LE.dll", "", "Stats.Stat", "specialTag");
    kStatTags = game::FieldOffset("LE.dll", "", "Stats.Stat", "tags");
    kStatExtraTag = game::FieldOffset("LE.dll", "", "Stats.Stat", "extraTag");
    kStatAdded = game::FieldOffset("LE.dll", "", "Stats.Stat", "addedValue");
    kStatIncreased = game::FieldOffset("LE.dll", "", "Stats.Stat", "increasedValue");
    kStatMoreValues = game::FieldOffset("LE.dll", "", "Stats.Stat", "moreValues");
    g_layoutReady = m_playerActor && g_mutatorOffset && g_myStatsOffset && g_statClass && m_statCtor && m_updateStats &&
        kStatsListOffset && kStatsNeedUpdate && kStatProperty && kStatSpecialTag && kStatTags && kStatExtraTag &&
        kStatAdded && kStatIncreased && kStatMoreValues;
    g_sheetInstance = game::FindStaticField("LE.dll", "", "CharacterSheet", "instance");
    g_sheetStats = game::FieldOffset("LE.dll", "", "CharacterSheet", "characterStats");
    g_textOffset = game::FieldOffset("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "m_text");
    g_displayClass = game::FindClass("LE.dll", "", "CharacterStatDisplay");
    m_gameObject = game::FindMethod("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_gameObject", 0);
    // Select the nongeneric overload by signature: the two-argument generic
    // overload (bool, List<T>) has the same name and arity.
    if (const il2cpp::Class* go = game::FindClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject")) {
        void* iter = nullptr;
        const auto& a = il2cpp::api();
        while (const Method* m = a.class_get_methods(go, &iter)) {
            if (std::string(a.method_get_name(m)) != "GetComponentsInChildren" || a.method_get_param_count(m) != 2) continue;
            char* type = a.type_get_name(a.method_get_param(m, 0));
            const bool match = type && std::string(type) == "System.Type";
            if (type) a.free(type);
            if (match) { m_displayChildren = {m, *reinterpret_cast<void* const*>(m)}; break; }
        }
    }
    g_displayProperty = game::FieldOffset("LE.dll", "", "CharacterStatDisplay", "property");
    g_displayTags = game::FieldOffset("LE.dll", "", "CharacterStatDisplay", "tags");
    g_displayType = game::FieldOffset("LE.dll", "", "CharacterStatDisplay", "displayType");
    g_displayText = game::FieldOffset("LE.dll", "", "CharacterStatDisplay", "statText");
    for (int i = 0; i < 7; ++i)
        g_resistanceOffsets[i] = game::FieldOffset("LE.dll", "", "CharacterSheet", kResistanceFields[i]);
    Log("stat: layout %s; constructor RVA 0x%llX, UpdateStatsInternal RVA 0x%llX",
        g_layoutReady ? "resolved" : "MISSING (editor disabled)",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(m_statCtor.code) - il2cpp::api().base),
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(m_updateStats.code) - il2cpp::api().base));
    h_chargeTick.ref = game::FindMethod("LE.dll", "", "PlayerChargeManager", "OnUpdateTick", 1);
    g_chargeOwner = game::FieldOffset("LE.dll", "", "ChargeManager", "actor");
    g_actorCharges = game::FieldOffset("LE.dll", "", "Actor", "chargeManager");
    if (!g_chargeOwner || !g_actorCharges) h_chargeTick.ref = {};

    Log("player: PlayerFinder.getPlayerActor %s, Actor.characterMutator +0x%zX, CharacterMutator.myStats +0x%zX; PlayerChargeManager.OnUpdateTick "
        "%s (local owner checked)",
        m_playerActor ? "found" : "MISSING", g_mutatorOffset, g_myStatsOffset, h_chargeTick.ref ? "found" : "MISSING");
    return static_cast<bool>(m_playerActor) && static_cast<bool>(h_chargeTick.ref);
}

std::string SetSpeed(double m) {
    if (!std::isfinite(m) || m < 1 || m > kMaxSpeed) return "speed: refused: multiplier must be between 1 and 5";
    if (!game::IsOfflinePlay()) { ++g_speedRefused; return "speed: refused: " + game::GateText(); }
    const std::string reply = statedit::Set({9, 0, 0, 0}, statedit::Mode::Increased, m - 1);
    if (reply.rfind("EpochPact ", 0) != 0) return reply;
    g_speedValue = m; g_speedPercent = m - 1; ++g_speedApplied;
    return "speed -> x" + std::to_string(m) + "; " + reply;
}

std::string SetCooldown(double m) { return feature::Set(g_cooldown, m); }

std::string StatList() {
    std::string out = "stat names:";
    for (const StatDef& d : kStatDefs) {
        out += ' ';
        out += d.name;
    }
    out += "\nusage: stat <name> [raw value]   (fire 0.75 -> 75%; parry 0.5 -> 50%; reflect 10 -> 1000%; bowattackspeed 5 -> +500% increased)";
    return out;
}

std::string ReadStat(const std::string& name) {
    const StatDef* def = FindStatDef(name);
    if (!def) return "stat: unknown name (run stat or statraw for the complete property list)";
    return statedit::Read({def->sp, def->tags, 0, 0});
}

std::string SetStat(const std::string& name, double value) {
    const StatDef* def = FindStatDef(name);
    if (!def) return "stat: unknown name (run stat or statraw for the complete property list)";
    return statedit::Set({def->sp, def->tags, 0, 0}, def->increased ? statedit::Mode::Increased : statedit::Mode::Added, value);
}

std::string SheetProbe() {
    return OnMain([] {
        void* sheet = game::StaticObject(g_sheetInstance);
        if (!sheet || !g_sheetStats || !g_textOffset || !game::IsAlive(sheet)) return std::string("sheetread: sheet unavailable");
        if (*reinterpret_cast<void**>(static_cast<char*>(sheet) + g_sheetStats) != PlayerBaseStats())
            return std::string("sheetread: open the character sheet for the current player first");
        std::string out = "sheetread:";
        for (int i = 0; i < 7; ++i) {
            if (!g_resistanceOffsets[i]) continue;
            void* label = *reinterpret_cast<void**>(static_cast<char*>(sheet) + g_resistanceOffsets[i]);
            out += std::string(" ") + kResistanceFields[i] + "=";
            out += LabelText(label);
        }
        return out;
    });
}

std::string SheetStats() { return statedit::Catalog(); }

namespace {

std::string StatScanImpl(int sp) {
    if (!g_layoutReady) return "statscan: stat layout unavailable";
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
