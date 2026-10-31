#pragma once

#include <Structs/Inventory.h>

#include <optional>
#include <vector>

// What changed in an actor's stacks of an item, as item changes (a count per stack, its extra data and all): for the
// engine's in-place changes our item hooks do not see (tempering 16145, enchanting 16146; party-items.md).
namespace ItemChanges
{
[[nodiscard]] inline std::vector<Inventory::Entry> Diff(const std::vector<Inventory::Entry>& acBefore, const std::vector<Inventory::Entry>& acAfter)
{
    std::vector<Inventory::Entry> changes;
    const auto cAdd = [&](const Inventory::Entry& acStack, int32_t aSign) {
        for (auto& change : changes)
            if (change.CanBeMerged(acStack))
            {
                change.Count += aSign * acStack.Count;
                return;
            }
        Inventory::Entry change = acStack;
        change.Count *= aSign;
        changes.push_back(change);
    };
    for (const auto& cStack : acBefore)
        cAdd(cStack, -1);
    for (const auto& cStack : acAfter)
        cAdd(cStack, 1);
    std::erase_if(changes, [](const Inventory::Entry& acChange) { return acChange.Count == 0; });
    return changes;
}

// The carried stack a removal of acAsked takes from: the same extra data, worn or not. The engine removes from a stack
// only when given that stack's own list (InventoryChanges::RemoveItem 16059), never a list rebuilt from an entry.
[[nodiscard]] inline std::optional<size_t> FindStack(const std::vector<Inventory::Entry>& acCarried, const Inventory::Entry& acAsked)
{
    for (size_t i = 0; i < acCarried.size(); ++i)
    {
        Inventory::Entry carried = acCarried[i];
        carried.ExtraWorn = acAsked.ExtraWorn;
        carried.ExtraWornLeft = acAsked.ExtraWornLeft;
        if (carried.CanBeMerged(acAsked))
            return i;
    }
    return std::nullopt;
}
} // namespace ItemChanges
