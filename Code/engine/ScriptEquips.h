#pragma once

#include <items/InventoryChangeEvent.h>

#include <optional>

// Actor.EquipItem (54661) adds an item the actor lacks straight to its inventory changes (16053), past our add hooks
// (docs/re/systems/inventory-changes.md, "A script's EquipItem"). Pure rule, shared by the hook and the fake engine.
namespace ScriptEquips
{
// The arrival to report once the native returned: one added by it (held now, not before). Not worn yet: 54661 queues
// its equip (bQueueEquip). The story's when a quest's script called it, which every player then wears (party-items.md).
[[nodiscard]] inline std::optional<InventoryChangeEvent> Arrival(uint32_t aActor, const GameId& acItem, int32_t aBefore, int32_t aAfter,
                                                                 bool aStoryScript) noexcept
{
    if (aBefore > 0 || aAfter <= 0 || acItem == GameId{})
        return std::nullopt;
    Inventory::Entry item{};
    item.BaseId = acItem;
    item.Count = aAfter;
    InventoryChangeEvent event(aActor, std::move(item));
    event.StoryGiven = aStoryScript;
    event.StoryWorn = aStoryScript;
    return event;
}
} // namespace ScriptEquips
