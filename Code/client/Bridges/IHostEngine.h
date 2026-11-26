#pragma once

#include <Followers/State.h>
#include <Games/Skyrim/DetectionRules.h>
#include <Messages/CellStateRequest.h>
#include <Messages/StoryEventRequest.h>
#include <Messages/WorldState.h>
#include <ScriptValues.h>
#include <Services/Host/HostBodiesGame.h>
#include <Services/Host/HostTimeGame.h>
#include <Structs/GameId.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Payloads
{
struct TrapContact;
}
namespace PartyThreat
{
struct State;
}
struct ContainerTransferRequest;
struct SummonRequest;

/**
 * @brief The host's game as the host's rules read and change it (HostGame's engine side), in session ids.
 * Only while hosting. The engine's is HostEngine; the tests' is FakeHostEngine.
 */
class IHostEngine
{
public:
    virtual ~IHostEngine() = default;

    [[nodiscard]] virtual std::optional<std::vector<Followers::State>> ReadFollowers() const noexcept = 0;
    [[nodiscard]] virtual std::vector<Followers::Order> TakeFollowerOrders() const noexcept = 0;
    virtual void SetFollowerTargets(const std::vector<Followers::Target>& acTargets) const noexcept = 0;

    virtual void SetPartyThreats(const PartyThreat::State& acState) const noexcept = 0;

    // The weather the game shows; none for no sky, or the world map's own.
    [[nodiscard]] virtual std::optional<HostSky> ReadWeather() const noexcept = 0;
    // The region's own weather (TESRegion, weather.md); none for no such region or one without weathers.
    [[nodiscard]] virtual std::optional<GameId> RegionWeather(const GameId& acRegion) const noexcept = 0;
    // What this game's NPCs see of that character's copy (StealthEye::Read); none when its copy is not loaded.
    [[nodiscard]] virtual std::optional<DetectionRules::EyeReading> ReadEye(uint32_t aSessionId) const noexcept = 0;
    // Every global but the constants; the calendar's are marked.
    [[nodiscard]] virtual std::vector<HostGlobal> ReadGlobals() const noexcept = 0;
    // Only globals written since the last call; constants excluded, calendar marked.
    [[nodiscard]] virtual std::vector<HostGlobal> TakeGlobalChanges() const noexcept = 0;
    // Every quest as the game holds it now.
    [[nodiscard]] virtual std::vector<QuestState> ReadQuests() const noexcept = 0;
    // Only quests whose state or alias references changed since the last call.
    [[nodiscard]] virtual std::vector<QuestState> TakeQuestChanges() const noexcept = 0;
    // Runs the clock on to that hour, the next day's if it is earlier, as a wait does.
    virtual void SetHour(float aHours) noexcept = 0;
    virtual void AdvanceTime(float aHours) noexcept = 0;
    // A world script's event on an object, kept for the others: it activated a door (by a reference) or played an
    // animation on it; or a container's items changed (not an actor's).
    virtual void RecordActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aOpenState) noexcept = 0;
    // aPullerFormId: the player whose pull of a lever furniture it was (its own game played it), else 0.
    virtual void RecordAnimation(uint32_t aObjectFormId, const std::string& acAnimation, const std::string& acEvent, uint32_t aPullerFormId = 0) noexcept = 0;
    virtual void RecordItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept = 0;
    // A guest's activation, run by its player's copy (TESObjectREFR::ActivateRef 19796, our hook passing it by).
    virtual void Activate(const GameId& acObject, uint32_t aNpcId, uint32_t aActivatorId) noexcept = 0;
    // A guest's summon on its copy, cast by the copy (HostGame::Summon): the engine's SummonCreatureEffect makes it.
    virtual void Summon(uint32_t aCasterId, const SummonRequest& acSummon) noexcept = 0;
    // A guest's lock change, accepted: the host's object takes it, and the others get it as the host's change.
    // A trap touched a guest's player in its game (docs/design/traps.md).
    virtual void TouchTrap(const Payloads::TrapContact& acContact) noexcept = 0;
    virtual void SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept = 0;
    // A guest's container change (kItems), accepted: the host's container holds that many, its stacks with extra data
    // as given, and the others get it.
    virtual void SetItems(const ObjectChange& acItems) noexcept = 0;
    // A guest's take from or store into a container, moved here natively: what moved, signed as the request's count.
    [[nodiscard]] virtual Inventory::Entry TransferContainer(const ContainerTransferRequest&, uint32_t aPlayer) noexcept = 0;
    // A guest's story event queued in this game's story manager, judged for its character (party-model.md rule 4).
    virtual void RaiseStoryEvent(const StoryEvent& acEvent) noexcept = 0;
    // A guest's drop (docs/design/dropped-items.md): made here with its count and extra data, where its cell is loaded;
    // its created id (it goes out with this game's created references); none while the cell is not loaded here, an empty
    // id when its base or cell is not in this game.
    [[nodiscard]] virtual std::optional<GameId> CreateItem(const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) noexcept = 0;
    // A guest's pickup granted: the party's loose item taken out of this world (disabled and deleted, the deletion goes
    // out), what it held; none when it is gone.
    [[nodiscard]] virtual std::optional<Inventory::Entry> TakeItem(const GameId& acObject) noexcept = 0;
    // A guest's drop of its mirror: this game's player drops the real stack where its copy stands (shared-items.md).
    // The created id; an empty id refuses (a quest object, an alias's elsewhere); none: no real stack.
    [[nodiscard]] virtual std::optional<GameId> DropPartyItem(const Inventory::Entry& acItem, uint32_t aPlayer) noexcept = 0;
    // A loose item a quest alias holds (ExtraAliasInstanceArray), a quest object or not, picked up by this game's player
    // for the party (shared-items.md): the alias follows it into the player's inventory. False for any other item.
    [[nodiscard]] virtual bool TakeQuestObject(const GameId& acObject) noexcept = 0;
    // Since the last call: each reference the game created, the state of each one the funnel saw change, the
    // physics objects that came to rest, the records that lost rows, then the scripts' events in order.
    [[nodiscard]] virtual std::vector<ObjectChange> TakeObjectChanges() noexcept = 0;
    // HostGame::TakeTouches: the bodies the host's player pushed or hit (PlayerHands).
    [[nodiscard]] virtual std::vector<BodyKey> TakeTouches() noexcept = 0;
    // The references TakeObjectChanges saw the game itself move (CHANGE_REFR_MOVE: a script, the console) since the last call.
    [[nodiscard]] virtual std::vector<std::pair<GameId, glm::vec3>> TakePlaced() noexcept = 0;
    // The physics bodies physics moves: the awake ones read now, and those that fell asleep (HostGame::TakeBodies); an
    // actor's ragdoll by its session id (aSessionOf: 0 for an actor the guests do not have).
    [[nodiscard]] virtual HostBodyReads TakeBodies(uint64_t aTick, const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept = 0;
    // A created container's contents (HostGame::ReadContents), its leveled entries rolled first.
    [[nodiscard]] virtual std::optional<ObjectChange> ReadContents(const GameId& acObject) const noexcept = 0;
    // Every row of each reference in the cell that this game, or a guest's (acChanged), holds changes for, then each
    // container's contents after this game rolled it; none while the cell is not attached here.
    [[nodiscard]] virtual std::optional<std::vector<ObjectChange>> ReadCellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const noexcept = 0;
    // A guest's own script's call that changes the world, made here for that guest (aGuestFormId: its copy), as a
    // script's call; aOnReturn is told what it returned. False: it could not be queued.
    virtual bool RunFor(uint32_t aGuestFormId, const LocalCall& acCall, std::function<void(LocalValue)> aOnReturn) noexcept = 0;
};
