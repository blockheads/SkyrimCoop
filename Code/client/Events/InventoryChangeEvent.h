#pragma once

#include <Structs/Inventory.h>

/**
 * @brief Dispatched when the contents of an object or actor inventory changes locally.
 *
 * The event has a Drop member variable, since dropped items need to be handled differently.
 */
struct InventoryChangeEvent
{
    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem)
        : FormId(aFormId)
        , Item(std::move(arItem))
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aDrop)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , Drop(aDrop)
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aDrop, bool aUpdateClients)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , Drop(aDrop)
        , UpdateClients(aUpdateClients)
    {
    }

    uint32_t FormId{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
    // Added by a quest's script (a quest, alias, dialogue line or scene): the story gave it (party-items.md).
    bool StoryGiven = false;
};
