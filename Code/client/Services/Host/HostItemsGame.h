#pragma once

#include <Messages/WorldState.h>
#include <Structs/GameId.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct ContainerTransferRequest;

// What the host's rules read and change in its game: the party's items and gold, containers and loose items. A part of HostGame.
struct HostItemsGame
{
    // The party's gold: the host's player's (party-purse.md); a change to it, silently (a removal takes at most all).
    std::function<uint32_t()> ReadPartyGold;
    std::function<void(int32_t aCount)> AddPartyGold;
    // The party's items (party-items.md): the host's player's count of that stack (its extra data), a change to it
    // silently, every party stack it carries, and an item's name.
    std::function<int32_t(const Inventory::Entry&)> ReadPartyItem;
    std::function<void(const Inventory::Entry&)> AddPartyItem;
    std::function<std::vector<Inventory::Entry>()> ReadPartyItems;
    std::function<std::string(const GameId&)> ItemName;
    // An item's form type (a key: PartyItems::cKeyFormType), for deciding a guest's arrival.
    std::function<uint8_t(const GameId&)> ItemFormType;
    // A guest's drop of its mirror of a party item: the host's player drops the real one where that player's copy stands
    // (its created id); an empty id refuses it (the real one kept); none: the old way (removed, made anew from the drop).
    std::function<std::optional<GameId>(const Inventory::Entry& acItem, uint32_t aPlayer)> DropPartyItem;
    // Native transfer between a container and the guest's copy; returns the signed amount actually moved. IsQuestItem: the
    // host's player's own stack moved instead (an alias's item taken into it, a party item's real stack put from it).
    std::function<Inventory::Entry(const ContainerTransferRequest&, uint32_t aPlayer)> TransferContainer;
    // A guest's drop: the host's game makes the item (its count and extra data) there, the party's one reference; its
    // created id, none while that cell is not loaded here, an empty id when it cannot be made (no such item here).
    std::function<std::optional<GameId>(const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement)> CreateItem;
    // A guest picks up the party's loose item: taken out of the host's world (the deletion goes out), what it held;
    // none when it is gone (another player was first).
    std::function<std::optional<Inventory::Entry>(const GameId& acObject)> TakeItem;
    // A loose item a quest alias holds the host's player picks up for the party (shared-items.md); false for any other.
    std::function<bool(const GameId& acObject)> TakeQuestObject;
};
