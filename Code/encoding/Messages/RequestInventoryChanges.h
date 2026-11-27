#pragma once

#include "Message.h"
#include <Structs/Inventory.h>
#include <Wire/Buffer.h>

struct RequestInventoryChanges final : MessageOf<RequestInventoryChanges, kRequestInventoryChanges>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(SessionId, Item, Drop, UpdateClients, NewPartyItem, StoryGiven, AliasHeld, Wear);
    }
    bool operator==(const RequestInventoryChanges&) const noexcept = default;

    uint32_t SessionId{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
    // A party item arriving, decided by the host's game (shared-items.md): its stack renamed in place; the story gave it.
    bool NewPartyItem = false;
    bool StoryGiven = false;
    // A guest's arrival: its stack a quest alias holds (the host decides whether it is the party's).
    bool AliasHeld = false;
    // The story put it on (a quest's script's EquipItem): every player wears its party copy.
    bool Wear = false;
};
