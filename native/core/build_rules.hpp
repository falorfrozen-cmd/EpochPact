#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ep::builds::rules {
// Derive budgets from natural progress, never from the last modified budget.
// Flooring the total carries fractional gains across levels without a saved counter.
inline int Budget(int natural, double multiplier, int flat = 0, int limit = 65535) {
    if (natural < 0 || !std::isfinite(multiplier) || multiplier < 1 || flat < 0) return natural;
    const double value = std::floor(static_cast<double>(natural) * multiplier) + flat;
    return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(limit)));
}
inline int PassiveBudget(int natural, int level, double multiplier) {
    const int fromLevels = std::clamp(level - 2, 0, natural);
    return std::min(65535, Budget(fromLevels, multiplier) + (natural - fromLevels));
}
inline int SkillXp(int gain, double multiplier) {
    return gain <= 0 ? gain : Budget(gain, multiplier, 0, std::numeric_limits<int>::max());
}
inline int ExtraProjectiles(int nativeExtra, double multiplier, int flat) {
    // At least one projectile already exists. Scale the total, not only the extras.
    return nativeExtra < 0 ? nativeExtra : std::max(0, Budget(nativeExtra + 1, multiplier, flat, 64) - 1);
}
// Blessing choices after a Monolith boss. The game fills
// numberOfStandardBlessings + extraBlessings option slots (extras come from
// corruption) from a generator it always asks for 3 + extraBlessings. Raise the
// standard part so the total reaches the requested count; never lower what the
// game offers, so the generator always returns at least as many as are filled.
inline int BlessingStandard(int standard, int extra, int requested) {
    if (requested <= 0 || extra < 0 || standard < 0) return standard;
    return std::max(standard, requested - extra);
}
// The reward panel shows this many option slots (it hides slots 4 and 5 below it).
inline int BlessingShown(int native, int requested) {
    return requested > 0 && native >= 0 ? std::max(native, requested) : native;
}
// Summon counts. The game reads a summon skill's limit while it summons and
// unsummons its lowest-priority minions above that limit. The extra raises the
// limit for that summon only; the kind of summon decides which extra applies.
enum class SummonKind { Minion, Totem, Ballista, Forged, Companion };
constexpr uint32_t TotemTag = 16384;  // AT.Totem
inline SummonKind KindOf(bool companion, bool ballista, bool forged, uint32_t tags, bool countsAsTotem) {
    if (companion) return SummonKind::Companion;  // limited by the companion limit instead
    if (ballista) return SummonKind::Ballista;
    if (forged) return SummonKind::Forged;
    if ((tags & TotemTag) || countsAsTotem) return SummonKind::Totem;
    return SummonKind::Minion;
}
constexpr int MaximumSummonLimit = 200;
// Never lowers what the game allows; the raised total stays bounded.
inline int RaisedLimit(int native, int extra) {
    if (native < 0 || extra <= 0 || native >= MaximumSummonLimit) return native;
    return std::min(MaximumSummonLimit, native + extra);
}
// The companion limit (the game multiplies it by 60 for each companion's share).
constexpr int MaximumCompanions = 20;
inline int RaisedCompanions(int native, int extra) {
    if (native < 0 || extra <= 0 || native >= MaximumCompanions) return native;
    return std::min(MaximumCompanions, native + extra);
}
// Primordial items a character may wear: the game allows one plus a bonus.
// "Any number" is more than there are equipment slots; never lowered.
constexpr int UnlimitedPrimordials = 99;
inline int PrimordialLimit(int native, bool unlimited) {
    if (!unlimited || native < 0 || native >= UnlimitedPrimordials) return native;
    return UnlimitedPrimordials;
}
// How il2cpp_field_set_value expects a field's source, from the IL2CPP type
// code of the field (Il2CppTypeEnum): a reference field takes the reference
// itself, a primitive value field takes the address of its data. Anything else
// (structs, generic value types, pointers) is not copied field by field.
enum class FieldCopy { Value, Reference, Unsupported };
inline FieldCopy CopyKind(int typeCode) {
    if (typeCode >= 0x02 && typeCode <= 0x0d) return FieldCopy::Value;  // BOOLEAN .. R8
    switch (typeCode) {
    case 0x0e:  // STRING
    case 0x12:  // CLASS
    case 0x14:  // ARRAY
    case 0x15:  // GENERICINST (List<T> here; checked by the caller's field list)
    case 0x1c:  // OBJECT
    case 0x1d:  // SZARRAY
        return FieldCopy::Reference;
    default:
        return FieldCopy::Unsupported;
    }
}
}
