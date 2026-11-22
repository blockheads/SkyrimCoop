#pragma once

#include <cstdint>
#include <optional>

namespace EquipmentRepair
{
// FormType::Weapon; the RightHand and LeftHand BGSEquipSlot forms.
constexpr uint8_t kWeaponType = 41;
constexpr uint32_t kRightHand = 0x13F42;
constexpr uint32_t kLeftHand = 0x13F43;

// A weapon goes in its hand; everything else (armor, a torch, ammo) in its own default slot (0).
constexpr uint32_t EquipSlot(uint8_t aType, bool aLeft) noexcept
{
    return aType == kWeaponType ? (aLeft ? kLeftHand : kRightHand) : 0;
}

// The hand a worn item is held in (0 left, 1 right): a weapon's own, a torch the left; none for anything else.
constexpr std::optional<uint32_t> HandOf(uint8_t aType, bool aLeft) noexcept
{
    constexpr uint8_t cLight = 31;
    if (aType == kWeaponType)
        return aLeft ? 0u : 1u;
    if (aType == cLight)
        return 0u;
    return std::nullopt;
}

// A hand the owner holds a spell in holds something on a loaded copy: EquipSpell (38928) without a process only
// selects the spell, leaving the hand empty (docs/re/systems/copy-hand-spells.md).
constexpr bool HandHolds(uint32_t aWantedSpell, uint32_t aHeld) noexcept
{
    return !aWantedSpell || aHeld;
}

// A copy's worn set made its owner's: only the pieces that differ off and on; cleared and equipped again whole when the
// engine refused one (after a model rebuild, copy-equipment-after-rebuild.md) or the hand spells differ (the reset puts
// the owner's spells back).
template <class Set, class Read, class Reset, class Equip, class Unequip>
void Apply(const Set& acWanted, Read&& aRead, bool aSameHands, Reset&& aReset, Equip&& aEquip, Unequip&& aUnequip)
{
    const Set cCurrent = aRead();
    if (acWanted == cCurrent && aSameHands)
        return;
    if (aSameHands)
    {
        for (const auto& item : cCurrent)
            if (!acWanted.contains(item))
                aUnequip(item.first, item.second);
        for (const auto& item : acWanted)
            if (!cCurrent.contains(item))
                aEquip(item.first, item.second);
        if (aRead() == acWanted)
            return;
    }
    aReset();
    for (const auto& item : acWanted)
        aEquip(item.first, item.second);
}
} // namespace EquipmentRepair
