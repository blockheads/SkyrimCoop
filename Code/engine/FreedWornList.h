#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

// Worn stacks' lists that 19694's unequip freed while InventoryChanges::RemoveItem (16059) still holds them: 19694
// always answers "survived", and 16059 hands the list to its nested calls, the first of which frees it again
// (docs/re/systems/inventory-changes.md, "the list freed twice"). One per thread.
class FreedWornList
{
public:
    // RemoveItem entered and returned (it calls itself): each running one has its own freed list.
    void Enter() noexcept
    {
        if (m_depth < m_freed.size())
            m_freed[m_depth] = nullptr;
        ++m_depth;
    }
    void Leave() noexcept
    {
        --m_depth;
        if (m_depth < m_freed.size())
            m_freed[m_depth] = nullptr; // the address may be a new list's by the next removal
    }

    // After 19694 unequipped apList inside a RemoveItem: acHeld is the lists the item's entry holds now.
    template <class Lists> void Unequipped(const void* apList, const Lists& acHeld) noexcept
    {
        if (m_depth == 0 || m_depth > m_freed.size())
            return;
        const bool cHeld = std::find(std::begin(acHeld), std::end(acHeld), apList) != std::end(acHeld);
        m_freed[m_depth - 1] = cHeld ? nullptr : apList;
    }

    // Whether a list handed to a RemoveItem is one a running RemoveItem's unequip freed.
    [[nodiscard]] bool IsFreed(const void* apList) const noexcept
    {
        const auto cEnd = m_freed.begin() + std::min<size_t>(m_depth, m_freed.size());
        return apList && std::find(m_freed.begin(), cEnd, apList) != cEnd;
    }

private:
    std::array<const void*, 8> m_freed{};
    uint32_t m_depth = 0;
};
