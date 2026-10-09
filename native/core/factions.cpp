#include "factions.hpp"
#include "managed.hpp"
#include <sstream>

namespace ep::factions {
using namespace managed;
namespace {
game::MethodRef Resolve(const char* cls, const char* name, int arity, const char* ns = "LE.Factions") {
    auto method = game::FindMethod("LE.dll", ns, cls, name, arity);
    if (!method) throw std::runtime_error(std::string("missing method: ") + cls + "." + name);
    return method;
}
std::string Ranks(void* data) {
    Root ranks(Invoke(Method(data, "get_FactionRanks", 0), data));
    const auto count = Get<uintptr_t>(ranks.Get(), 0x18);
    if (count > 100) throw std::runtime_error("invalid faction rank catalog");
    std::ostringstream out; out << '[';
    for (uintptr_t i = 0; i < count; ++i) {
        void* rank = Get<void*>(ranks.Get(), 0x20 + i * sizeof(void*));
        if (!rank) throw std::runtime_error("missing faction rank");
        if (i) out << ',';
        out << "{\"index\":" << i << ",\"optionalRankValue\":" << Get<int>(rank, Offset("FactionData.RankData", "optionalRankValue", "LE.Factions"))
            << ",\"reputationRequired\":" << Get<int>(rank, Offset("FactionData.RankData", "reputationRequired", "LE.Factions"))
            << ",\"title\":" << Json(Text(Get<void*>(rank, Offset("FactionData.RankData", "rankTitle", "LE.Factions"))))
            << ",\"description\":" << Json(Text(Get<void*>(rank, Offset("FactionData.RankData", "rankDescription", "LE.Factions"))))
            << ",\"lines\":[";
        bool first = true;
        void* lines = Get<void*>(rank, Offset("FactionData.RankData", "rankDescriptionLines", "LE.Factions"));
        if (lines) for (void* line : Entries(lines)) { if (!first) out << ','; first = false; out << Json(Text(line)); }
        out << "]}";
    }
    out << ']'; return out.str();
}
std::string Prophecies(void* faction) {
    Root slots(Invoke(Method(faction, "get_ProphecySlots", 0), faction));
    // The public IReadOnlyList is backed by the game's ProphecySlot[4].
    const auto count = Get<uintptr_t>(slots.Get(), 0x18);
    if (count > 8) throw std::runtime_error("invalid prophecy slot array");
    std::ostringstream out; out << '[';
    for (uintptr_t i = 0; i < count; ++i) {
        if (i) out << ',';
        void* slot = Get<void*>(slots.Get(), 0x20 + i * sizeof(void*));
        if (!slot) { out << "null"; continue; }
        const auto lens = Offset("ProphecySlot", "<Lens>k__BackingField", "LE.Factions");
        const auto rankMethod = Resolve("CircleOfFortune", "GetRankToUnlockProphecySlot", 1);
        int index = static_cast<int>(i); void* args[]{&index};
        out << "{\"index\":" << i << ",\"rankRequired\":" << Value<int>(rankMethod, nullptr, args)
            << ",\"charges\":" << static_cast<int>(Get<uint8_t>(slot, Offset("ProphecySlot", "<CompletedCharges>k__BackingField", "LE.Factions")))
            << ",\"chargeProgress\":" << Get<int>(slot, Offset("ProphecySlot", "<ChargeInProgress>k__BackingField", "LE.Factions"))
            << ",\"lens\":";
        if (Get<bool>(slot, lens)) out << Get<int>(slot, lens + 4); else out << "null";
        out << ",\"rewardSelected\":" << Boolean(Get<void*>(slot, Offset("ProphecySlot", "<Reward>k__BackingField", "LE.Factions")) != nullptr) << '}';
    }
    out << ']'; return out.str();
}
}
std::string Read() {
    return Run([] {
        if (!game::IsOfflinePlay()) throw std::runtime_error(game::GateText());
        RequireSession(4);
        Root actor(Invoke(Resolve("PlayerFinder", "getPlayerActor", 0, "")));
        if (!game::IsAlive(actor.Get())) throw std::runtime_error("enter a zone with an offline character first");
        void* tracker = Get<void*>(actor.Get(), Offset("Actor", "characterDataTracker"));
        void* character = Get<void*>(tracker, Offset("CharacterDataTracker", "charData"));
        if (!Value<bool>(Resolve("CharacterData", "get_IsOffline", 0, "LE.Data"), character))
            throw std::runtime_error("loaded character is not offline");
        void* provider = Get<void*>(actor.Get(), Offset("Actor", "factionInfo"));
        const auto* getFaction = Method(provider, "TryGetFaction", 2);
        const auto dataMethod = Resolve("FactionsList", "GetFactionDataByID", 1);
        std::ostringstream out;
        out << "{\"ok\":true,\"player\":{\"id\":" << Json(Text(Invoke(Method(character, "get_Id", 0), character)))
            << ",\"name\":" << Json(Text(Get<void*>(character, Offset("CharacterData", "<CharacterName>k__BackingField", "LE.Data")))) << "},\"factions\":[";
        const char* names[]{"CircleOfFortune", "MerchantsGuild", "ForgottenKnights", "TheWeaver"};
        for (uint8_t id = 0; id < 4; ++id) {
            if (id) out << ',';
            void* dataArgs[]{&id}; Root data(Invoke(dataMethod, nullptr, dataArgs));
            void* faction = nullptr; void* args[]{&id, &faction};
            const bool found = Value<bool>(getFaction, provider, args);
            out << "{\"id\":" << static_cast<int>(id) << ",\"name\":" << Json(names[id]) << ",\"ranks\":" << Ranks(data.Get());
            if (found && faction) {
                out << ",\"member\":" << Boolean(Get<bool>(faction, Offset("Faction", "<IsMember>k__BackingField", "LE.Factions")))
                    << ",\"rank\":" << Get<int>(faction, Offset("Faction", "<Rank>k__BackingField", "LE.Factions"))
                    << ",\"favor\":" << Get<int>(faction, Offset("Faction", "<Favor>k__BackingField", "LE.Factions"))
                    << ",\"reputation\":" << Get<int>(faction, Offset("Faction", "<Reputation>k__BackingField", "LE.Factions"));
                if (!id) out << ",\"prophecySlots\":" << Prophecies(faction);
                if (id == 3) {
                    out << ",\"weaverPoints\":{\"earned\":" << Value<int>(Method(faction, "get_EarnedWeaverPoints", 0), faction)
                        << ",\"fromRank\":" << Value<int>(Method(faction, "get_EarnedWeaverPointsFromRank", 0), faction)
                        << ",\"fromEchoes\":" << Value<int>(Method(faction, "get_EarnedWeaverPointsFromWovenEchoes", 0), faction)
                        << ",\"maxRank\":" << Value<int>(Resolve("TheWeaver", "get_MaxWeaverPointsFromRank", 0))
                        << ",\"maxEchoes\":" << Value<int>(Resolve("TheWeaver", "get_MaxWeaverPointsFromWovenEchoes", 0))
                        << ",\"maxTotal\":" << Value<int>(Resolve("TheWeaver", "get_MaxWeaverPoints", 0)) << '}';
                    void* completed = Get<void*>(faction, Offset("TheWeaver", "completedWovenEchoTypes", "LE.Factions"));
                    const auto* size = il2cpp::api().class_get_field_from_name(il2cpp::api().object_get_class(completed), "_size");
                    if (!size) throw std::runtime_error("Woven completion list unavailable");
                    out << ",\"completedWovenEchoTypes\":" << Get<int>(completed, il2cpp::api().field_get_offset(size))
                        << ",\"corruptionGainDisabled\":" << Boolean(Value<bool>(Method(faction, "get_EchoesDisableCorruptionIncreaseWhenKillingShade", 0), faction));
                }
            }
            out << '}';
        }
        out << "],\"prophecyData\":{";
        Root prophecy(Invoke(Resolve("ProphecyData", "get_Data", 0)));
        const char* fields[]{"celerityLensExtraCharge", "charityLensExtraCharge", "tyrannyLensChargeMultiplier",
            "duplicationLensChance", "celestialScalesDuplicationChance", "celestialScalesGroleEggReplacementChance",
            "originLensPrefixTierUpgradeChance", "finalityLensSuffixTierUpgradeChance", "prowessLensLevelOfSkillsWeightModifier",
            "qualityLensLegendaryPotentialRollModifier", "qualityLensRandomAffixTierUpgradeChance",
            "anomalyLensAffixWeightDilutionExponent", "curiosityLensUniqueWeightDilutionExponent"};
        bool first = true;
        for (const char* field : fields) {
            if (!first) out << ','; first = false;
            out << Json(field) << ':' << Get<float>(prophecy.Get(), Offset("ProphecyData", field, "LE.Factions"));
        }
        out << ",\"lenses\":[";
        const auto* lensClass = game::FindClass("LE.dll", "LE.Factions", "ProphecySlot.LensType");
        if (!lensClass) throw std::runtime_error("lens enum unavailable");
        first = true; void* iter = nullptr;
        while (const auto* field = il2cpp::api().class_get_fields(lensClass, &iter)) {
            if (!(il2cpp::api().field_get_flags(field) & il2cpp::kFieldLiteral)) continue;
            int value = 0; il2cpp::api().field_static_get_value(field, &value);
            void* args[]{&value};
            if (!first) out << ','; first = false;
            out << "{\"id\":" << value << ",\"name\":" << Json(il2cpp::api().field_get_name(field))
                << ",\"rankRequired\":" << Value<int>(Resolve("ProphecyData", "GetUnlockRankForLens", 1), prophecy.Get(), args)
                << ",\"effect\":" << Json(Text(Invoke(Resolve("ProphecyData", "GetLocalizedLensEffect", 1), nullptr, args))) << '}';
        }
        out << "]}}"; return out.str();
    });
}
}
