#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

// How many of an item a reference holds, as CommonLibSSE-NG's GetInventory counts it (what the menus show): its base
// container's count, unless its changes entry is a roll's (an ExtraLeveledItem list), plus the entry's count. Not
// 16108: its 16214 reads an entry at 0 over no base count as one, and a chest keeps such an entry once a roll's item is
// taken (16059 frees it only on an actor). docs/re/systems/inventory-changes.md, "A roll's taken item".
[[nodiscard]] constexpr int32_t ContainerCount(int32_t aBase, std::optional<int32_t> aEntry, bool aRolled) noexcept
{
    if (!aEntry)
        return std::max(aBase, 0);
    return std::max((aRolled ? 0 : aBase) + *aEntry, 0);
}
