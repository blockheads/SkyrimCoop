#pragma once

#include <cstdint>

// The engine took off what an actor wore and put its base's outfit on (TESNPC::ResetWornToOutfit 418622), past our
// equip hooks, or a copy was moved to its owner's cell (its hand spells dropped, copy-hand-spells.md): a copy wears its
// owner's again, an owner reads its items again (InventoryService).
struct WornResetEvent
{
    uint32_t FormId{};
};
