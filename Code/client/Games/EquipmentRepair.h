#pragma once

#include <cstdint>
#include <optional>

namespace EquipmentRepair
{
// FormType::Weapon; the RightHand and LeftHand BGSEquipSlot forms.
constexpr uint8_t kWeaponType = 41;
constexpr uint32_t kRightHand = 0x13F42;
constexpr uint32_t kLeftHand = 0x13F43;
constexpr uint32_t kBothHands = 0x13F45;

constexpr uint8_t kLightType = 31;
constexpr uint8_t kLeftHandBit = 1, kRightHandBit = 2;

// A one-handed weapon goes in its hand, a two-hander in BothHands (passed: no slot is RightHand, 38911, which a
// dual-wield race keeps, 38926); armor, a torch or ammo in its own slot (0): a torch's LeftHand (17619). hand-slots.md.
constexpr uint32_t EquipSlot(uint8_t aType, bool aLeft, bool aTwoHanded = false) noexcept
{
    if (aType != kWeaponType)
        return 0;
    return aTwoHanded ? kBothHands : aLeft ? kLeftHand : kRightHand;
}

// The hands a worn item is held in (kLeftHandBit, kRightHandBit): a one-handed weapon its own, a torch the left (an
// equip's only hand for it, 17619), a two-hander both unless the left holds something else worn (Aela's [torch, bow],
// measured). 0 for anything else.
constexpr uint8_t HandsOf(uint8_t aType, bool aLeft, bool aTwoHanded = false, bool aLeftTaken = false) noexcept
{
    if (aType == kWeaponType && aTwoHanded)
        return aLeftTaken ? kRightHandBit : kLeftHandBit | kRightHandBit;
    if (aType == kWeaponType)
        return aLeft ? kLeftHandBit : kRightHandBit;
    return aType == kLightType ? kLeftHandBit : 0;
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
