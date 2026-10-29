#pragma once
#include <Structs/Inventory.h>
struct World;

namespace ContainerTransfers
{
// A guest's container menu take or store, proposed to the host instead of moved here (TESObjectREFR's switch, off
// while a guest: GuestEngine::SetDecidesWorld). False: not one to propose.
bool Propose(World& aWorld, uint32_t aSource, uint32_t aTarget, Inventory::Entry aItem, bool aSteal = false) noexcept;
// Worn and quest flags belong to the holder; the remaining extra data identifies the stack.
inline Inventory::Entry Stack(Inventory::Entry aItem) noexcept
{
    aItem.ExtraWorn = aItem.ExtraWornLeft = aItem.IsQuestItem = false;
    return aItem;
}
} // namespace ContainerTransfers
