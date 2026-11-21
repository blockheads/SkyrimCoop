#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

// An item's count as CommonLibSSE-NG's GetInventory gives it: the base's count unless the entry is a roll's, plus the
// entry's. Not 16214, which reads a taken roll item's 0 entry as one (inventory-changes.md, "A roll's taken item").
[[nodiscard]] constexpr int32_t ContainerCount(int32_t aBase, std::optional<int32_t> aEntry, bool aRolled) noexcept
{
    if (!aEntry)
        return std::max(aBase, 0);
    return std::max((aRolled ? 0 : aBase) + *aEntry, 0);
}
