#pragma once

#include <Bridges/BodyReads.h>
#include <Bridges/IGuestEngine.h>
#include <Games/Skyrim/DetectionRules.h>
#include <Bridges/PhysicsBodies.h>
#include <Messages/CellStateRequest.h>
#include <Messages/DroppedItems.h>
#include <Messages/Payloads.h>
#include <Messages/StoryEventRequest.h>
#include <Messages/WorldState.h>
#include <Structs/BodyFrame.h>
#include <Structs/GameId.h>
#include <Structs/TimeModel.h>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>
#include <NoCopyMove.h>

namespace PlayerHands
{
struct Push;
}

struct OwnStoryEvent;
struct TESObjectREFR;
struct TESQuest;

/**
 * @brief A guest's game taking the host's world, each with the engine's own call, from session ids.
 * Only while a guest: the host's world is its own.
 */
class GuestEngine final : public IGuestEngine
{
public:
    // aBodies finds the host's references through this game's while it lives.
    explicit GuestEngine(class IBodyBridge& aBodies) noexcept;
    ~GuestEngine() noexcept;

    NO_COPY_MOVE(GuestEngine);

    void SetCalendar(const TimeModel& acTime) noexcept override;
    bool HoldWeather(const GameId& acWeather) noexcept override;
    void ReleaseWeather() noexcept override;
    [[nodiscard]] GameId ReadWeatherRegion() const noexcept override;
    void HoldEye(const DetectionRules::EyeReading& acReading) noexcept override;
    void ReleaseEye() noexcept override;
    bool RunPlayerNative(const Payloads::RoutedCall& acCall, std::function<void(LocalValue)> aOnReturn) noexcept override;
    bool RunScenePackage(const GameId& acPackage) noexcept override;
    void EndScenePackage() noexcept override;
    bool SetGlobal(const GlobalValue& acGlobal) noexcept override;
    void ApplyQuest(const QuestState& acQuest, bool aAsLoad) noexcept override;
    void RetryQuestAliases() noexcept override;
    void SetDecidesWorld(bool aOn) noexcept override;
    [[nodiscard]] std::optional<StoryEvent> ReadStoryEvent(const OwnStoryEvent& acEvent) const noexcept override;
    bool ApplyObjectChange(const ObjectChange& acChange, bool aLoad) noexcept override;
    [[nodiscard]] bool HasLiveBody(const GameId& acObject) noexcept override;
    [[nodiscard]] std::optional<ObjectChange> ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) const noexcept override;
    [[nodiscard]] std::vector<uint32_t> AttachedCells() const noexcept override;
    [[nodiscard]] std::optional<CellStateRequest> ReadCellChanges(uint32_t aCellFormId) const noexcept override;
    [[nodiscard]] bool IsCellAttached(const GameId& acCell) const noexcept override;
    void FreePlaced() noexcept override;
    [[nodiscard]] bool IsPlacementPending(const GameId& acObject) const noexcept override;
    [[nodiscard]] std::vector<BodyKey> TakePushes() noexcept override;
    [[nodiscard]] std::vector<BodyKey> TakeHits() noexcept override;
    [[nodiscard]] std::vector<BodyFrame> ReadHeld(const SessionOf_t& acSessionOf) noexcept override;
    [[nodiscard]] OwnBodies TakeOwnBodies(uint64_t aTick, const SessionOf_t& acSessionOf) noexcept override;
    void FollowOwn(const BodyKey& acKey) noexcept override;
    [[nodiscard]] std::optional<DropItemRequest> ReadDrop(uint32_t aFormId) noexcept override;
    bool AdoptDrop(uint32_t aTag, const GameId& acObject) noexcept override;
    [[nodiscard]] std::vector<BodyKey> TakeAdoptedDrops() noexcept override;
    [[nodiscard]] bool Adopting(const GameId& acObject) const noexcept override;
    [[nodiscard]] std::optional<PickUpRequest> ReadPickUp(uint32_t aFormId) const noexcept override;
    void PickUp(const NotifyPickUp& acGrant) noexcept override;

private:
    // The reference here for a session id; the host's created ones (ObjectChange::kCreatedModId) through m_created.
    [[nodiscard]] TESObjectREFR* FindReference(const GameId& acId) const noexcept;
    // The reference the host's fill names here: the twin of one the host's game created, the plugin's in memory, or
    // read from the plugin as the host's quest read it (kept for aQuest).
    [[nodiscard]] TESObjectREFR* FindFillReference(TESQuest& aQuest, const QuestAliasFill& acFill) const noexcept;
    // The host's role fills, references and locations, on this game's quest.
    bool ApplyFills(TESQuest& aQuest, const QuestState& acQuest) const noexcept;
    // Makes this game's own of a reference the host's game created, once.
    bool Create(const ObjectChange& acChange) noexcept;
    // The host's rest of a physics object: put there and held (its bodies keyframed).
    bool PlaceAtHostRest(TESObjectREFR& aObject, const ObjectChange& acMove) noexcept;

    // The host's id of this game's reference (a created one's through m_created).
    [[nodiscard]] std::optional<GameId> HostIdOf(const TESObjectREFR& acObject) const noexcept;
    // The player's pushes or hits on the host's references' bodies, keyed, one per body.
    [[nodiscard]] std::vector<BodyKey> Keyed(const std::vector<PlayerHands::Push>& acPushes) const noexcept;
    IBodyBridge& m_bodyBridge; // the session's bodies here
    std::vector<std::pair<BodyKey, uint32_t>> m_placed; // held at a move row's rest: updates since
    BodyReads m_reads;
    PhysicsBodies m_moving; // the references and player's ragdoll this game's physics moves
    // The host's created references' ids (base id part) and this game's, by handle.
    std::unordered_map<uint32_t, uint32_t> m_created;
    // This game's player's drops the host has not made yet: form id (the tag), by handle.
    std::unordered_map<uint32_t, uint32_t> m_ownDrops;
    std::vector<BodyKey> m_adopted;
    // The host's created ids this game's player picked up: their deletion has nothing left to delete.
    std::unordered_set<uint32_t> m_taken;
    std::map<uint32_t, QuestState> m_pendingAliases;
    // Whether the reference is a loose item the host decides: a twin of the host's, or an own drop.
    [[nodiscard]] bool IsPartyItem(const TESObjectREFR& acObject) const noexcept;
};
