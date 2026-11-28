#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

// Unequips every worn stack without the engine's walk (InventoryChanges::VisitWornItems 16096, under UnequipAll 38899):
// an unequip that empties the entry list's first entry frees the node that walk holds next (16067), and the walk reads
// it (docs/re/crashes/2026-11-27-guest-cell-attach-null-virtual.md). The stacks are taken first; each is found again
// by address in a fresh read before it is unequipped, so nothing freed is followed.
namespace WornStacks
{
template <class TItem, class TList> struct Stack
{
    TItem* pItem;
    TList* pList;
    bool Left;
    int32_t Count;

    bool operator==(const Stack&) const = default;
};

// acRead(): the stacks worn now, a fresh walk. acUnequip(stack): unequips one, found worn in that walk. Returns how
// many were unequipped.
template <class TRead, class TUnequip> uint32_t UnequipAll(const TRead& acRead, const TUnequip& acUnequip)
{
    uint32_t unequipped = 0;
    for (const auto& cTaken : acRead())
    {
        const auto cNow = acRead();
        // Gone: an earlier unequip took it (its list merged into another, or freed).
        if (std::find(cNow.begin(), cNow.end(), cTaken) == cNow.end())
            continue;
        acUnequip(cTaken);
        ++unequipped;
    }
    return unequipped;
}

// UnequipAll, then acEngine() (the engine's UnequipAll 38899, for the hands' spells) only once nothing is worn, so
// its walk visits nothing. False when a stack stayed worn and acEngine was skipped.
template <class TRead, class TUnequip, class TEngine>
bool UnequipAllThenEngine(const TRead& acRead, const TUnequip& acUnequip, const TEngine& acEngine)
{
    UnequipAll(acRead, acUnequip);
    if (!acRead().empty())
        return false;
    acEngine();
    return true;
}
} // namespace WornStacks
