#pragma once

#include <cstdint>

// A worn stack's list that 19694's unequip freed while InventoryChanges::RemoveItem (16059) still holds it: 19694
// always answers "survived", and 16059 hands the list to its nested calls, the first of which frees it again
// (docs/re/systems/inventory-changes.md, "the list freed twice"). One per thread.
class FreedWornList
{
public:
    // RemoveItem entered and returned (it calls itself).
    void Enter() noexcept { ++m_depth; }
    void Leave() noexcept
    {
        // The RemoveItem that unequipped returned: the address may be a new list's by the next removal.
        if (m_depth == m_freedAt)
            m_pFreed = nullptr;
        --m_depth;
    }

    // After 19694 unequipped apList, inside a RemoveItem: whether the item's entry still holds it.
    void Unequipped(const void* apList, bool aStillHeld) noexcept
    {
        m_pFreed = aStillHeld ? nullptr : apList;
        m_freedAt = m_depth;
    }

    // Whether a list handed to a RemoveItem is the freed one.
    [[nodiscard]] bool IsFreed(const void* apList) const noexcept { return apList && apList == m_pFreed; }

private:
    const void* m_pFreed = nullptr;
    uint32_t m_depth = 0;
    uint32_t m_freedAt = 0;
};
