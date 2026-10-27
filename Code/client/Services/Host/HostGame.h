#pragma once

#include <Messages/CellStateRequest.h>
#include <Messages/JoinMessages.h>
#include <Messages/CrimeMessages.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/WorldState.h>
#include <Structs/BodyFrame.h>
#include <Structs/GameVersions.h>
#include <Structs/PlayerRecord.h>
#include <Structs/Mods.h>
#include <Structs/TimeModel.h>

#include <functional>
#include <optional>

// The host's physics bodies since the last read (docs/design/physics-sync.md): those awake, read now, and those that
// fell asleep, where they rest.
struct HostBodies
{
    std::vector<BodyFrame> Awake;
    std::vector<BodyPose> Rested;
};

/**
 * @brief The host's game as HostAuthority sees it: what it reads and changes in the engine. The
 * host's services provide it (HostService); the headless tests a fake.
 */
struct HostGame
{
    // Native host adjudication of a guest action, and the party's per-faction bounty.
    std::function<bool(uint32_t, const CrimeRequest&)> JudgeCrime;
    std::function<std::vector<CrimeGold>()> ReadCrimeGold;
    std::function<std::vector<CrimeConfiscation>()> TakeCrimeConfiscations;
    // The host's own actors as its engine shows them now, but aExcludedSessionId.
    std::function<std::vector<NotifyCharacterSpawn>(uint32_t aExcludedSessionId)> OwnSpawns;
    // The host's copy of another player's character (position, items, hands, action) into the spawn, its values left
    // as its owner sent them; false when it has no copy.
    std::function<bool(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn)> ReadCopy;
    // The host's clock (the session's) and setting its hour.
    std::function<TimeModel()> ReadTime;
    std::function<void(float aHours)> SetHour;
    // The host's weather, when it shows one to share (none: no sky, or the map's).
    std::function<std::optional<GameId>()> ReadWeather;
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
    // The state of a cell's references the host's game changed, or aChanged names (a guest's own changes); none
    // while that cell is not loaded here.
    std::function<std::optional<std::vector<ObjectChange>>(const GameId& acCell, const std::vector<ChangedReference>& acChanged)>
        ReadCellState;
    // The host's world's physics bodies: awake ones read now, and those that fell asleep since the last call.
    std::function<HostBodies()> TakeBodies;
    // Where the host's game shows a guest's character (its copy), by player id; none without one.
    std::function<std::optional<glm::vec3>(uint32_t aPlayerId)> ReadPlayerPosition;
    // A guest's player's hand on the host's body: pushed as the guest's character pushed it there; let go (its holder's
    // frames the host's game played, BodyService), simulated again from that frame. False: no such body here.
    std::function<bool(const BodyPush& acPush)> PushBody;
    std::function<bool(const BodyFrame& acFrame)> FreeBody;
    // The host's bodies where they are now; none for one not here.
    std::function<std::vector<BodyPose>(const std::vector<BodyKey>& acKeys)> ReadBodies;
    // A guest's lock change, accepted into the host's world (it then goes out as the host's change).
    std::function<void(const GameId& acObject, bool aLocked, uint8_t aLockLevel)> SetLock;
    // A guest's container change, accepted: the host's container holds that many (and it goes out as the host's change).
    std::function<void(const GameId& acObject, const GameId& acItem, uint32_t aCount)> SetItems;
    // Saves the host's game now for a game joining from the main menu: the save in chunks, none when the engine
    // refused to save (HostSaveTransfer tries again next update).
    std::function<std::optional<std::vector<NotifySaveChunk>>()> SaveWorld;
    // The guests' characters for the host's save (CharacterRecords): a guest's record, and its place at a save.
    std::function<void(const PlayerRecord& acRecord)> StoreRecord;
    std::function<bool(const std::string& acKey)> HasRecord;
    std::function<void(const std::string& acKey, const GameId& acWorldSpace, const GameId& acCell, const std::array<float, 3>& acPosition)> StorePlace;
    // The host's difficulty (0 Novice to 5 Legendary): the session's.
    std::function<uint32_t()> ReadDifficulty;
    // The whole party is down: the host's game loads its previous save (HostService's wipe reload).
    std::function<void()> PartyWipe;
    // The host's Skyrim and SKSE, and its plugins in load order: every player's must be the same.
    GameVersions Versions;
    Mods Plugins;
};
