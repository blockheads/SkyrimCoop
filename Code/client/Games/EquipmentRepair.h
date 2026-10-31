#pragma once

#include <chrono>
#include <cstdint>

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

// When this game sends its actors' worn sets: on the update after an equip change, else every 3 s.
struct Snapshots
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point Last{};
    bool Changed = false;

    void OnChange() noexcept { Changed = true; }
    bool SendAt(Clock::time_point aNow) noexcept
    {
        if (!Changed && aNow - Last < std::chrono::seconds(3))
            return false;
        Last = aNow;
        Changed = false;
        return true;
    }
};

// A copy's worn set differing from its owner's: cleared and equipped again whole.
template <class Set, class Reset, class Equip>
void Apply(const Set& acWanted, const Set& acCurrent, Reset&& aReset, Equip&& aEquip)
{
    if (acWanted == acCurrent)
        return;
    aReset();
    for (const auto& item : acWanted)
        aEquip(item.first, item.second);
}
} // namespace EquipmentRepair
