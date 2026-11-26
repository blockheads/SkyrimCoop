#pragma once

#include <Messages/CellStateRequest.h>
#include <Messages/WorldState.h>
#include <Structs/GameId.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// What the host's rules read and change in its game: the host's world: globals, quests, objects and cells. A part of HostGame.
struct HostWorldGame
{
    // The host's global variables but the constants.
    std::function<std::vector<HostGlobal>()> ReadGlobals;
    // Global writes since the last call, coalesced to their current values.
    std::function<std::vector<HostGlobal>()> TakeGlobalChanges;
    // Every quest of the host's game as it holds it now.
    std::function<std::vector<QuestState>()> ReadQuests;
    // Quest writes since the last call, coalesced to their current state.
    std::function<std::vector<QuestState>()> TakeQuestChanges;
    // The host's world objects' changes since the last call (references' state, scripts' events, containers' items), in order.
    std::function<std::vector<ObjectChange>()> TakeObjectChanges;
    // The references the host's game itself placed since the last call (a script's or the console's move, not physics).
    // Each with where it was put.
    std::function<std::vector<std::pair<GameId, glm::vec3>>()> TakePlaced;
    // The state of a cell's references the host's game changed, or aChanged names (a guest's own changes); none
    // while that cell is not loaded here.
    std::function<std::optional<std::vector<ObjectChange>>(const GameId& acCell, const std::vector<ChangedReference>& acChanged)>
        ReadCellState;
    // Everything a created container of the host's holds (kContents, its roll made now); none for no container.
    std::function<std::optional<ObjectChange>(const GameId& acObject)> ReadContents;
    // A guest's activation of an object (aObject) or an NPC (aNpcId), run by its player's copy (aActivatorId); what
    // changed goes out as the host's rows.
    std::function<void(const GameId& acObject, uint32_t aNpcId, uint32_t aActivatorId)> Activate;
    // A guest's lock change, accepted into the host's world (it then goes out as the host's change).
    std::function<void(const GameId& acObject, bool aLocked, uint8_t aLockLevel)> SetLock;
    // A guest's container change, accepted (a kItems change): the host's container holds that many, its stacks with
    // extra data as given (and it goes out as the host's change).
    std::function<void(const ObjectChange& acItems)> SetItems;
};
