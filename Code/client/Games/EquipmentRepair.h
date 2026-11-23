#pragma once

#include <cstdint>
#include <optional>

namespace EquipmentRepair
{
// FormType::Weapon; the RightHand and LeftHand BGSEquipSlot forms.
constexpr uint8_t kWeaponType = 41;
constexpr uint32_t kRightHand = 0x13F42;
constexpr uint32_t kLeftHand = 0x13F43;

constexpr uint8_t kLightType = 31;
constexpr uint8_t kLeftHandBit = 1, kRightHandBit = 2;

// A one-handed weapon goes in its hand; a two-hander, armor, a torch or ammo in its own slot (0): a two-hander's
// BothHands fills both hands, a torch's LeftHand the left (17619). docs/re/systems/hand-slots.md.
constexpr uint32_t EquipSlot(uint8_t aType, bool aLeft, bool aTwoHanded = false) noexcept
{
    return aType == kWeaponType && !aTwoHanded ? (aLeft ? kLeftHand : kRightHand) : 0;
}

// The hands a worn item is held in (kLeftHandBit, kRightHandBit), as a forced worn list's 3D load holds it (24736):
// a weapon or torch worn left the left, worn right the right; a two-handed weapon both. 0 for anything else.
constexpr uint8_t HandsOf(uint8_t aType, bool aLeft, bool aTwoHanded = false) noexcept
{
    if (aType == kWeaponType && aTwoHanded)
        return kLeftHandBit | kRightHandBit;
    if (aType == kWeaponType || aType == kLightType)
        return aLeft ? kLeftHandBit : kRightHandBit;
    return 0;
}

// A torch worn right: no equip holds it there (a light's slot is LeftHand, 17619), only a 3D load's (24736).
constexpr bool HeldByLoadOnly(uint8_t aType, bool aLeft) noexcept
{
    return aType == kLightType && !aLeft;
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
