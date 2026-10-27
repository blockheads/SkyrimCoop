#pragma once

#include <Bridges/PhysicsBodies.h>
#include <Messages/CellStateRequest.h>
#include <Messages/DroppedItems.h>
#include <Messages/Payloads.h>
#include <Messages/WorldState.h>
#include <Structs/BodyFrame.h>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>
#include <NoCopyMove.h>

struct TESObjectREFR;
struct TESQuest;

/**
 * @brief A guest's game taking the host's world, each with the engine's own call, from session ids.
 * Only while a guest: the host's world is its own.
 */
class GuestEngine
{
public:
    // aBodies finds the host's references through this game's while it lives.
    explicit GuestEngine(class IBodyBridge& aBodies) noexcept;
    ~GuestEngine() noexcept;

    NO_COPY_MOVE(GuestEngine);

    // The calendar as the engine's clock leaves it (Calendar::Update 36291), date and days passed included.
    void SetCalendar(const TimeModel& acTime) noexcept;
    // Shows the weather, again after the game changed its own; leaves the world map's alone. False: no such weather.
    bool HoldWeather(const GameId& acWeather) noexcept;
    // The game's own weather again.
    void ReleaseWeather() noexcept;
    // Runs a native on the player the host's script called for this game's player (Game.AdvanceSkill,
    // Game.QueryStat; party-model.md, rule 2), as a script's call does; aOnReturn is told what it returned. False: a
    // form it names is not here.
    bool RunPlayerNative(const Payloads::RoutedCall& acCall, std::function<void(LocalValue)> aOnReturn = {}) noexcept;
    // Sets it as Papyrus' GlobalVariable.SetValue (55923) does; false for none or a constant.
    bool SetGlobal(const GlobalValue& acGlobal) noexcept;
    // Makes the quest so, each stage without its fragment (docs/re/systems/quest-apply.md); aAsLoad: as a save
    // load does, the fields only. Fills of references the host's game created are left as they are.
    void ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept;
    // Off while a guest, the host's game deciding instead: its story manager starts no quest, its world's
    // scripts do not react to its player (activations, triggers), and it rolls no leveled container or placed
    // leveled item.
    void SetDecidesWorld(bool aOn) noexcept;
    // Makes the host's world object change here: activates the door (if it is as the host's was), plays the
    // animation, enables or disables the reference, sets its lock, makes the container hold that many. False: the
    // object, the activator or the item is not in this game.
    // aLoad: a cell's state, not a change; a state already as the host's is left alone (no events for it).
    bool ApplyObjectChange(const ObjectChange& acChange, bool aLoad = false) noexcept;
    // This game's container's count of the item now (Bridges/ContainerItems.h), for a proposal to the host.
    [[nodiscard]] std::optional<ObjectChange> ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept;
    // The cells loaded here now and done loading (the interior, or the exterior grid's attached ones), by form id.
    [[nodiscard]] std::vector<uint32_t> AttachedCells() const noexcept;
    // Asks for the host's state of that loaded cell, naming the references this game holds changes for; none for a
    // cell no longer loaded or not in the session's mods.
    [[nodiscard]] std::optional<CellStateRequest> ReadCellChanges(uint32_t aCellFormId) const noexcept;
    [[nodiscard]] bool IsCellAttached(const GameId& acCell) const noexcept;
    // Each update: the references placed at the host's rest (a move row) an update ago, simulated again and asleep
    // there, once the engine has put their held bodies at their nodes.
    void FreePlaced() noexcept;
    // Shared rest refreshes restore record flags after their placements have finished.
    [[nodiscard]] bool IsPlacementPending(const GameId& acObject) const noexcept;
    // This game's player's pushes of the host's references' bodies since the last call (its character's,
    // PlayerHands), one per body.
    [[nodiscard]] std::vector<BodyPush> TakePushes() noexcept;
    // The host's reference this game's player holds (Activate held: PlayerCharacter::grabbedObject): its dynamic
    // bodies read now; none while it holds nothing of the host's (a body the host's frames still move is keyframed, so
    // the engine's grab refuses it).
    [[nodiscard]] std::vector<BodyFrame> ReadHeld() const noexcept;
    // What this game's own physics moves: the host's references' bodies awake and simulated here, where they are
    // (Moved); this game's player's ragdoll while it simulates (dead), its awake bodies read now (Ragdoll) or every
    // body once all sleep (RagdollRested), keyed by index alone.
    struct OwnBodies
    {
        std::vector<BodyPose> Moved;
        std::vector<BodyFrame> Ragdoll;
        std::vector<BodyPose> RagdollRested;
    };
    [[nodiscard]] OwnBodies TakeOwnBodies() noexcept;

    // Dropped items (docs/design/dropped-items.md). This game's player's drop: the item (count, extra data) and where,
    // tagged with its reference here, now a drop of its own until the host's answer; none for no loose item.
    [[nodiscard]] std::optional<DropItemRequest> ReadDrop(uint32_t aFormId) noexcept;
    // The host made drop aTag as acObject: this game's reference is its twin (an empty id: its own again, not the
    // host's to grant). False: no longer here (picked up).
    bool AdoptDrop(uint32_t aTag, const GameId& acObject) noexcept;
    // The bodies of drops adopted since the last call: this game's own until the host's rest (GuestHands).
    [[nodiscard]] std::vector<BodyKey> TakeAdoptedDrops() noexcept;
    // This game's player reached for a loose item the host decides (a twin, or its own drop): what to ask for.
    [[nodiscard]] std::optional<PickUpRequest> ReadPickUp(uint32_t aFormId) const noexcept;
    // The host granted it: this game's player picks up its twin, or gets the item when the twin is gone.
    void PickUp(const NotifyPickUp& acGrant) noexcept;

private:
    // The reference here for a session id; the host's created ones (ObjectChange::kCreatedModId) through m_created.
    [[nodiscard]] TESObjectREFR* FindReference(const GameId& acId) const noexcept;
    // The reference the host's fill names here: the twin of one the host's game created, the plugin's in memory, or
    // read from the plugin as the host's quest read it (kept for aQuest).
    [[nodiscard]] TESObjectREFR* FindFillReference(TESQuest& aQuest, const QuestAliasFill& acFill) const noexcept;
    // The host's role fills, references and locations, on this game's quest.
    void ApplyFills(TESQuest& aQuest, const QuestState& acQuest) const noexcept;
    // Makes this game's own of a reference the host's game created, once.
    bool Create(const ObjectChange& acChange) noexcept;
    // The host's rest of a physics object: put there and held (its bodies keyframed).
    bool PlaceAtHostRest(TESObjectREFR& aObject, const ObjectChange& acMove) noexcept;

    // The host's id of this game's reference (a created one's through m_created).
    [[nodiscard]] std::optional<GameId> HostIdOf(const TESObjectREFR& acObject) const noexcept;
    IBodyBridge& m_bodyBridge; // the session's bodies here
    std::vector<std::pair<BodyKey, uint32_t>> m_placed; // held at a move row's rest: updates since
    PhysicsBodies m_moving; // the references and player's ragdoll this game's physics moves
    // The host's created references' ids (base id part) and this game's, by handle.
    std::unordered_map<uint32_t, uint32_t> m_created;
    // This game's player's drops the host has not made yet: form id (the tag), by handle.
    std::unordered_map<uint32_t, uint32_t> m_ownDrops;
    std::vector<BodyKey> m_adopted;
    // The host's created ids this game's player picked up: their deletion has nothing left to delete.
    std::unordered_set<uint32_t> m_taken;
    // Whether the reference is a loose item the host decides: a twin of the host's, or an own drop.
    [[nodiscard]] bool IsPartyItem(const TESObjectREFR& acObject) const noexcept;
};
