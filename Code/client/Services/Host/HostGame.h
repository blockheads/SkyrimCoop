#pragma once

#include <Messages/CellStateRequest.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/WorldState.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>
#include <Structs/TimeModel.h>

#include <functional>
#include <optional>

/**
 * @brief The host's game as HostAuthority sees it: what it reads and changes in the engine. The
 * host's services provide it (HostService); the headless tests a fake.
 */
struct HostGame
{
    // The host's own actors as its engine shows them now, but aExcludedSessionId.
    std::function<std::vector<NotifyCharacterSpawn>(uint32_t aExcludedSessionId)> OwnSpawns;
    // The host's copy of another player's character (position, values, items, hands, action) into
    // the spawn; false when it has no copy.
    std::function<bool(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn)> ReadCopy;
    // The host's clock (the session's) and setting its hour.
    std::function<TimeModel()> ReadTime;
    std::function<void(float aHours)> SetHour;
    // The host's weather, when it shows one to share (none: no sky, or the map's).
    std::function<std::optional<GameId>()> ReadWeather;
    // The host's global variables but the constants.
    std::function<std::vector<HostGlobal>()> ReadGlobals;
    // Every quest of the host's game as it holds it now.
    std::function<std::vector<QuestState>()> ReadQuests;
    // The host's world objects' changes since the last call (references' state, scripts' events, containers' items), in order.
    std::function<std::vector<ObjectChange>()> TakeObjectChanges;
    // The state of a cell's references the host's game changed, or aChanged names (a guest's own changes); none
    // while that cell is not loaded here.
    std::function<std::optional<std::vector<ObjectChange>>(const GameId& acCell, const std::vector<ChangedReference>& acChanged)>
        ReadCellState;
    // A guest's lock change, accepted into the host's world (it then goes out as the host's change).
    std::function<void(const GameId& acObject, bool aLocked, uint8_t aLockLevel)> SetLock;
    // A guest's container change, accepted: the host's container holds that many (and it goes out as the host's change).
    std::function<void(const GameId& acObject, const GameId& acItem, uint32_t aCount)> SetItems;
    // The host's difficulty (0 Novice to 5 Legendary): the session's.
    std::function<uint32_t()> ReadDifficulty;
    // The host's Skyrim and SKSE, and its plugins in load order: every player's must be the same.
    GameVersions Versions;
    Mods Plugins;
};
