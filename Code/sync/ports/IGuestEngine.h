#pragma once

#include <crime/DetectionRules.h>
#include <Messages/CellStateRequest.h>
#include <Messages/DroppedItems.h>
#include <Messages/Payloads.h>
#include <Messages/StoryEventRequest.h>
#include <Messages/WorldState.h>
#include <core/ScriptValues.h>
#include <Structs/BodyFrame.h>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

struct OwnStoryEvent;

/**
 * @brief A guest's game taking the host's world, from session ids. Only while a guest: the host's world is its own.
 * The engine's is GuestEngine; the tests' is FakeGuestEngine.
 */
class IGuestEngine
{
public:
    virtual ~IGuestEngine() = default;

    // The calendar as the engine's clock leaves it (Calendar::Update 36291), date and days passed included.
    virtual void SetCalendar(const TimeModel& acTime) noexcept = 0;
    // Shows the weather as the forced one, faded into (26241), again after the game changed its own, and keeps it from
    // expiring (Sky::s_pHeldWeather); leaves the world map's alone. False: no such weather.
    virtual bool HoldWeather(const GameId& acWeather) noexcept = 0;
    // The game's own weather again.
    virtual void ReleaseWeather() noexcept = 0;
    // This game's player's weather region (Sky::PlayerWeatherRegion); none for none.
    [[nodiscard]] virtual GameId ReadWeatherRegion() const noexcept = 0;
    // The eye's reads of this game's player answer what the host's NPCs see of it (StealthEye::Hold), until released.
    virtual void HoldEye(const DetectionRules::EyeReading& acReading) noexcept = 0;
    virtual void ReleaseEye() noexcept = 0;
    // Runs a native on the player the host's script called for this game's player (Game.AdvanceSkill,
    // Game.QueryStat; party-model.md, rule 2), as a script's call does; aOnReturn is told what it returned. False: a
    // form it names is not here.
    virtual bool RunPlayerNative(const Payloads::RoutedCall& acCall, std::function<void(LocalValue)> aOnReturn = {}) noexcept = 0;
    // This game's player plays a part the host's player plays in an engine scene (cutscenes.md step 2): AI-driven, it
    // runs that package. False: no such package here. Ended: its controls take it back.
    virtual bool RunScenePackage(const GameId& acPackage) noexcept = 0;
    virtual void EndScenePackage() noexcept = 0;
    // Sets it as Papyrus' GlobalVariable.SetValue (55923) does; false for none or a constant.
    virtual bool SetGlobal(const GlobalValue& acGlobal) noexcept = 0;
    // Makes the quest so, each stage without its fragment (docs/re/systems/quest-apply.md); aAsLoad: as a save
    // load does, the fields only. Created actors use their session mapping; a missing copy is retried.
    virtual void ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept = 0;
    // An actor's alias can arrive before its spawn: retry just those fills, without replaying stages.
    virtual void RetryQuestAliases() noexcept = 0;
    // Off while a guest, the host's game deciding instead: its story manager starts no quest, its world's
    // scripts do not react to its player (activations, triggers), it rolls no leveled container or placed
    // leveled item, its container menus' takes and stores are the host's (ContainerTransfers), and its story events
    // go to the host's story manager (OwnStoryEvent), and its own NPCs are held until the host spawns them (GuestNpcs).
    virtual void SetDecidesWorld(bool aOn) noexcept = 0;
    // This game's story event as the session names it; none for one the host's game raises itself (it names another
    // game's actor: a kill, an assault).
    [[nodiscard]] virtual std::optional<StoryEvent> ReadStoryEvent(const OwnStoryEvent& acEvent) const noexcept = 0;
    // Makes the host's world object change here: activates the door (if it is as the host's was), plays the
    // animation, enables or disables the reference, sets its lock, makes the container hold that many. False: the
    // object, the activator or the item is not in this game.
    // aLoad: a cell's state, not a change; a state already as the host's is left alone (no events for it).
    virtual bool ApplyObjectChange(const ObjectChange& acChange, bool aLoad = false) noexcept = 0;
    // That reference has a dynamic body loaded here: the body stream places it, not its move row.
    [[nodiscard]] virtual bool HasLiveBody(const GameId& acObject) noexcept = 0;
    // This game's container's count of the item now (sync/items/ContainerItems.h), for a proposal to the host.
    [[nodiscard]] virtual std::optional<ObjectChange> ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept = 0;
    // The cells loaded here now and done loading (the interior, or the exterior grid's attached ones), by form id.
    [[nodiscard]] virtual std::vector<uint32_t> AttachedCells() const noexcept = 0;
    // Asks for the host's state of that loaded cell, naming the references this game holds changes for; none for a
    // cell no longer loaded or not in the session's mods.
    [[nodiscard]] virtual std::optional<CellStateRequest> ReadCellChanges(uint32_t aCellFormId) const noexcept = 0;
    [[nodiscard]] virtual bool IsCellAttached(const GameId& acCell) const noexcept = 0;
    // Each update: the references placed at the host's rest (a move row) an update ago, simulated again and asleep
    // there, once the engine has put their held bodies at their nodes.
    virtual void FreePlaced() noexcept = 0;
    // Shared rest refreshes restore record flags after their placements have finished.
    [[nodiscard]] virtual bool IsPlacementPending(const GameId& acObject) const noexcept = 0;
    // This game's player's pushes of the host's references' bodies since the last call (its character's,
    // PlayerHands), one per body.
    [[nodiscard]] virtual std::vector<BodyKey> TakePushes() noexcept = 0;
    // Its weapon swings' hits on the host's references' bodies since the last call (PlayerHands::TakeHits), one per body.
    [[nodiscard]] virtual std::vector<BodyKey> TakeHits() noexcept = 0;
    // What this game's player holds (PlayerCharacter::grabbedObject): a host reference's bodies, or a copy's corpse's by
    // its session id, watched until they rest (40555 grabs a dead actor).
    using SessionOf_t = std::function<uint32_t(uint32_t aFormId)>;
    [[nodiscard]] virtual std::vector<BodyFrame> ReadHeld(const SessionOf_t& acSessionOf) noexcept = 0;
    // What this game's own physics moves: the host's references' bodies awake and simulated here, where they are
    // (Moved, and as frames in Objects), those that came to rest here (ObjectsRested: Havok's sleep or still, as on
    // the host); this game's player's ragdoll while it simulates (dead), its awake bodies read now (Ragdoll) or every
    // body once all sleep (RagdollRested), keyed by index alone; a copy's corpse its player grabbed, as an object's
    // bodies (Objects, ObjectsRested), keyed by the copy's session id.
    struct OwnBodies
    {
        std::vector<BodyPose> Moved;
        std::vector<BodyFrame> Objects;
        std::vector<BodyPose> ObjectsRested;
        std::vector<BodyFrame> Ragdoll;
        std::vector<BodyPose> RagdollRested;
    };
    [[nodiscard]] virtual OwnBodies TakeOwnBodies(uint64_t aTick, const SessionOf_t& acSessionOf) noexcept = 0;
    // A reference's body this game took as its own: read as moved from now, however little it has left where it woke.
    virtual void FollowOwn(const BodyKey& acKey) noexcept = 0;

    // Dropped items (docs/design/dropped-items.md). This game's player's drop: the item (count, extra data) and where,
    // tagged with its reference here, now a drop of its own until the host's answer; none for no loose item.
    [[nodiscard]] virtual std::optional<DropItemRequest> ReadDrop(uint32_t aFormId) noexcept = 0;
    // The host made drop aTag as acObject: this game's reference is its twin (an empty id: its own again, not the
    // host's to grant). False: no longer here (picked up).
    virtual bool AdoptDrop(uint32_t aTag, const GameId& acObject) noexcept = 0;
    // The bodies of drops adopted since the last call: this game's own until the host's rest (GuestHands).
    [[nodiscard]] virtual std::vector<BodyKey> TakeAdoptedDrops() noexcept = 0;
    // A drop adopted as acObject not yet taken (its body this game's own).
    [[nodiscard]] virtual bool Adopting(const GameId& acObject) const noexcept = 0;
    // This game's player reached for a loose item the host decides (a twin, or its own drop): what to ask for.
    [[nodiscard]] virtual std::optional<PickUpRequest> ReadPickUp(uint32_t aFormId) const noexcept = 0;
    // The host granted it: this game's player picks up its twin, or gets the item when the twin is gone.
    virtual void PickUp(const NotifyPickUp& acGrant) noexcept = 0;
};
