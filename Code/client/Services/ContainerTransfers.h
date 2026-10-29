#pragma once
#include <Structs/Inventory.h>
struct World;

namespace ContainerTransfers
{
// The guest's native container menu waits for the host instead of moving items locally.
bool Propose(World& aWorld, uint32_t aSource, uint32_t aTarget, Inventory::Entry aItem, bool aSteal = false) noexcept;
// Worn and quest flags belong to the holder; the remaining extra data identifies the stack.
inline Inventory::Entry Stack(Inventory::Entry aItem) noexcept
{
    aItem.ExtraWorn = aItem.ExtraWornLeft = aItem.IsQuestItem = false;
    return aItem;
}
} // namespace ContainerTransfers
