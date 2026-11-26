#pragma once

#include <cstdint>

// This game changed an actor's items silently (under ScopedInventoryOverride: the host's container transfer): its owner
// reads its items part again (InventoryService).
struct ItemsChangedEvent
{
    uint32_t FormId{};
};
