#pragma once

#include <Bridges/IHostEngine.h>
#include <Bridges/PhysicsBodies.h>

struct FakeEngine;
struct World;

// The host's game as the host's rules read and change it, on the fake engine (HostSeams.cpp): what HostEngine does
// in Skyrim.
class FakeHostEngine final : public IHostEngine
{
public:
    FakeHostEngine(World& aWorld, FakeEngine& aEngine) noexcept;

    std::optional<std::vector<Followers::State>> ReadFollowers() const noexcept override;
    std::vector<Followers::Order> TakeFollowerOrders() const noexcept override;
    void SetFollowerTargets(const std::vector<Followers::Target>& acTargets) const noexcept override;
    void SetPartyThreats(const PartyThreat::State& acState) const noexcept override;
    std::optional<HostSky> ReadWeather() const noexcept override;
    std::optional<GameId> RegionWeather(const GameId& acRegion) const noexcept override;
    std::optional<DetectionRules::EyeReading> ReadEye(uint32_t aSessionId) const noexcept override;
    std::vector<HostGlobal> ReadGlobals() const noexcept override;
    std::vector<HostGlobal> TakeGlobalChanges() const noexcept override;
    std::vector<QuestState> ReadQuests() const noexcept override;
    std::vector<QuestState> TakeQuestChanges() const noexcept override;
    void SetHour(float aHours) noexcept override;
    void AdvanceTime(float aHours) noexcept override;
    void RecordActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aOpenState) noexcept override;
    void RecordAnimation(uint32_t aObjectFormId, const std::string& acAnimation, const std::string& acEvent, uint32_t aPullerFormId) noexcept override;
    void RecordItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept override;
    void Activate(const GameId& acObject, uint32_t aNpcId, uint32_t aActivatorId) noexcept override;
    void Summon(uint32_t aCasterId, const SummonRequest& acSummon) noexcept override;
    void TouchTrap(const Payloads::TrapContact& acContact) noexcept override;
    void SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept override;
    void SetItems(const ObjectChange& acItems) noexcept override;
    Inventory::Entry TransferContainer(const ContainerTransferRequest&, uint32_t aPlayer) noexcept override;
    void RaiseStoryEvent(const StoryEvent& acEvent) noexcept override;
    std::optional<GameId> CreateItem(const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) noexcept override;
    std::optional<Inventory::Entry> TakeItem(const GameId& acObject) noexcept override;
    std::optional<GameId> DropPartyItem(const Inventory::Entry& acItem, uint32_t aPlayer) noexcept override;
    bool TakeQuestObject(const GameId& acObject) noexcept override;
    std::vector<ObjectChange> TakeObjectChanges() noexcept override;
    std::vector<BodyKey> TakeTouches() noexcept override;
    std::vector<std::pair<GameId, glm::vec3>> TakePlaced() noexcept override;
    HostBodyReads TakeBodies(uint64_t aTick, const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept override;
    std::optional<ObjectChange> ReadContents(const GameId& acObject) const noexcept override;
    std::optional<std::vector<ObjectChange>> ReadCellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const noexcept override;
    bool RunFor(uint32_t aGuestFormId, const LocalCall& acCall, std::function<void(LocalValue)> aOnReturn) noexcept override;

private:
    World& m_world;
    FakeEngine& m_engine;
    PhysicsBodies m_physics; // the real one over the fake engine's physics
};
