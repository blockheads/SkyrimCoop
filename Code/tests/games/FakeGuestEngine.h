#pragma once

#include <Bridges/BodyReads.h>
#include <Bridges/IGuestEngine.h>
#include <Bridges/PhysicsBodies.h>

#include <map>
#include <unordered_map>

// A guest's game taking the host's world, on the fake engine (GuestSeams.cpp): what GuestEngine does in Skyrim.
class FakeGuestEngine final : public IGuestEngine
{
public:
    explicit FakeGuestEngine(class IBodyBridge& aBodies) noexcept;
    ~FakeGuestEngine() noexcept override;

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
    IBodyBridge& m_bodyBridge;
    BodyReads m_reads;
    PhysicsBodies m_moving;
    std::unordered_map<uint32_t, uint32_t> m_created;  // the host's created id -> this game's twin
    std::unordered_map<uint32_t, uint32_t> m_ownDrops; // this game's drops the host has not answered
    std::vector<BodyKey> m_adopted;
    std::map<uint32_t, QuestState> m_pendingAliases;
};
