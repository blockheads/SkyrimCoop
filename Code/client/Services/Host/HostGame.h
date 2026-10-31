#pragma once

#include <Messages/CellStateRequest.h>
#include <Messages/JoinMessages.h>
#include <Messages/CrimeMessages.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/StoryEventRequest.h>
#include <Messages/WorldState.h>
#include <Structs/BodyFrame.h>
#include <Structs/GameVersions.h>
#include <Structs/PlayerRecord.h>
#include <Structs/Mods.h>
#include <Structs/TimeModel.h>

#include <Followers/State.h>
#include <functional>
#include <optional>

struct ContainerTransferRequest;

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
    // Vanilla follower aliases and completed native commands; only the host save keeps their desired anchors.
    std::function<std::optional<std::vector<Followers::State>>()> ReadFollowers;
    std::function<std::vector<Followers::Order>()> TakeFollowerOrders;
    std::function<Followers::Anchors&()> FollowerAnchors;
    std::function<void(const std::vector<Followers::Target>&)> SetFollowerTargets;
    // Native host adjudication of a guest action, and the party's per-faction bounty.
    std::function<bool(uint32_t, const CrimeRequest&)> JudgeCrime;
    std::function<std::vector<CrimeGold>()> ReadCrimeGold;
    std::function<std::vector<CrimeConfiscation>()> TakeCrimeConfiscations;
    // The party's gold: the host's player's (party-purse.md); a change to it, silently (a removal takes at most all).
    std::function<uint32_t()> ReadPartyGold;
    std::function<void(int32_t aCount)> AddPartyGold;
    // The party's items (party-items.md): the host's player's count of that stack (its extra data), a change to it
    // silently, every party stack it carries, and an item's name.
    std::function<int32_t(const Inventory::Entry&)> ReadPartyItem;
    std::function<void(const Inventory::Entry&)> AddPartyItem;
    std::function<std::vector<Inventory::Entry>()> ReadPartyItems;
    std::function<std::string(const GameId&)> ItemName;
    // The host's own actors as its engine shows them now, but aExcludedSessionId.
    std::function<std::vector<NotifyCharacterSpawn>(uint32_t aExcludedSessionId)> OwnSpawns;
    // The host's copy of another player's character (position, items, hands, action) into the spawn, its values left
    // as its owner sent them; false when it has no copy.
    std::function<bool(uint32_t aSessionId, NotifyCharacterSpawn& aSpawn)> ReadCopy;
    // The host's clock (the session's) and setting its hour.
    std::function<TimeModel()> ReadTime;
    std::function<void(float aHours)> SetHour;
    std::function<void(float aHours)> AdvanceTime;
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
    std::function<bool(const BodyFrame& acFrame, uint64_t aReleasedAt)> FreeBody;
    // The host's bodies where they are now; none for one not here.
    std::function<std::vector<BodyPose>(const std::vector<BodyKey>& acKeys)> ReadBodies;
    // A guest's lock change, accepted into the host's world (it then goes out as the host's change).
    std::function<void(const GameId& acObject, bool aLocked, uint8_t aLockLevel)> SetLock;
    // A guest's container change, accepted (a kItems change): the host's container holds that many, its stacks with
    // extra data as given (and it goes out as the host's change).
    std::function<void(const ObjectChange& acItems)> SetItems;
    // Native transfer between a container and the guest's copy; returns the signed amount actually moved.
    std::function<Inventory::Entry(const ContainerTransferRequest&, uint32_t aPlayer)> TransferContainer;
    // A guest's drop: the host's game makes the item (its count and extra data) there, the party's one reference; its
    // created id, none while that cell is not loaded here, an empty id when it cannot be made (no such item here).
    std::function<std::optional<GameId>(const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement)> CreateItem;
    // A guest picks up the party's loose item: taken out of the host's world (the deletion goes out), what it held;
    // none when it is gone (another player was first).
    std::function<std::optional<Inventory::Entry>(const GameId& acObject)> TakeItem;
    // A loose quest object (a quest's alias holds it) the host's player picks up for the party (party-items.md); false
    // for any other.
    std::function<bool(const GameId& acObject)> TakeQuestObject;
    // A guest's story event, queued in the host's story manager and judged for the guest's character (HostStory).
    std::function<void(const StoryEvent& acEvent)> RaiseStoryEvent;
    // Saves the host's game now for a game joining from the main menu: the save in chunks, none when the engine
    // refused to save (HostSaveTransfer tries again next update).
    std::function<std::optional<std::vector<NotifySaveChunk>>()> SaveWorld;
    // The guests' characters for the host's save (CharacterRecords): a guest's record, and its place at a save.
    std::function<void(const PlayerRecord& acRecord)> StoreRecord;
    std::function<void(const GameId& acSpell, bool aAdded)> StoreStorySpell;
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
