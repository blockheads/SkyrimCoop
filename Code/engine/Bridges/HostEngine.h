#pragma once

#include <ports/IHostEngine.h>
#include <crime/DetectionRules.h>
#include <Messages/CellStateRequest.h>
#include <Bridges/BodyReads.h>
#include <physics/PhysicsBodies.h>
#include <Messages/WorldState.h>
#include <game/HostGame.h>
#include <Structs/GameId.h>

#include <chrono>
#include <map>
#include <optional>
#include <unordered_set>

namespace Payloads
{
struct TrapContact;
struct TrapHit;
}

namespace PartyThreat { struct State; }
struct TESObjectREFR;
struct TESBoundObject;
struct ExtraDataList;
struct Actor;
struct ContainerTransferRequest;

/**
 * @brief The host's game as the host's rules read and change it (HostGame's engine side), in session ids.
 * Only while hosting: a guest's game never reads these. A form whose mod the session lacks is left out.
 */
class HostEngine final : public IHostEngine
{
public:
    // Watches the engine's change funnel (FormChanges) for the references' state while it lives; aBodies finds the
    // references the guests have through it.
    explicit HostEngine(class IBodyBridge& aBodies) noexcept;
    ~HostEngine() noexcept override;

    HostEngine(const HostEngine&) = delete;
    HostEngine& operator=(const HostEngine&) = delete;

    [[nodiscard]] std::optional<std::vector<Followers::State>> ReadFollowers() const noexcept override;
    [[nodiscard]] std::vector<Followers::Order> TakeFollowerOrders() const noexcept override;
    void SetFollowerTargets(const std::vector<Followers::Target>& acTargets) const noexcept override;
    void SetPartyThreats(const PartyThreat::State& acState) const noexcept override;
    [[nodiscard]] std::optional<HostSky> ReadWeather() const noexcept override;
    [[nodiscard]] std::optional<GameId> RegionWeather(const GameId& acRegion) const noexcept override;
    [[nodiscard]] std::optional<DetectionRules::EyeReading> ReadEye(uint32_t aSessionId) const noexcept override;
    [[nodiscard]] std::vector<HostGlobal> ReadGlobals() const noexcept override;
    [[nodiscard]] std::vector<HostGlobal> TakeGlobalChanges() const noexcept override;
    [[nodiscard]] std::vector<QuestState> ReadQuests() const noexcept override;
    [[nodiscard]] std::vector<QuestState> TakeQuestChanges() const noexcept override;
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
    [[nodiscard]] Inventory::Entry TransferContainer(const ContainerTransferRequest&, uint32_t aPlayer) noexcept override;
    void RaiseStoryEvent(const StoryEvent& acEvent) noexcept override;
    [[nodiscard]] std::optional<GameId> CreateItem(const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) noexcept override;
    [[nodiscard]] std::optional<Inventory::Entry> TakeItem(const GameId& acObject) noexcept override;
    [[nodiscard]] std::optional<GameId> DropPartyItem(const Inventory::Entry& acItem, uint32_t aPlayer) noexcept override;
    [[nodiscard]] bool TakeQuestObject(const GameId& acObject) noexcept override;
    [[nodiscard]] std::vector<ObjectChange> TakeObjectChanges() noexcept override;
    [[nodiscard]] std::vector<BodyKey> TakeTouches() noexcept override;
    [[nodiscard]] std::vector<std::pair<GameId, glm::vec3>> TakePlaced() noexcept override;
    [[nodiscard]] HostBodyReads TakeBodies(uint64_t aTick, const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept override;
    [[nodiscard]] std::optional<ObjectChange> ReadContents(const GameId& acObject) const noexcept override;
    [[nodiscard]] std::optional<std::vector<ObjectChange>> ReadCellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const noexcept override;
    bool RunFor(uint32_t aGuestFormId, const LocalCall& acCall, std::function<void(LocalValue)> aOnReturn) noexcept override;

private:
    // A guest's take of a stack a quest alias holds: moved to this game's player, alias and all (IsQuestItem set).
    [[nodiscard]] Inventory::Entry MoveAliasStack(TESObjectREFR& aContainer, TESBoundObject& aItem, ExtraDataList& aList, int32_t aCount,
                                                  const ContainerTransferRequest& acRequest) noexcept;
    // The guests have it: a plugin's reference, or one this game created that went out (kCreate).
    [[nodiscard]] bool IsShared(uint32_t aFormId) const noexcept;
    // The created references that went out and are gone here (deleted, freed, or their id now another's), as
    // deletions for the guests: a script's Delete() of one often records no change (it is never saved).
    void TakeDeletedCreated(std::vector<ObjectChange>& aChanges) noexcept;

    mutable std::unordered_set<uint32_t> m_pendingActorAliases;
    IBodyBridge& m_bodyBridge;
    BodyReads m_reads;
    PhysicsBodies m_bodies{m_reads};
    std::vector<std::pair<GameId, glm::vec3>> m_placed;
    // The last animation sent for each object animated with no 3D here (TrapRules::SendsAnimation).
    struct UnseenAnimation
    {
        std::pair<std::string, std::string> Played;
        std::chrono::steady_clock::time_point At;
    };
    std::unordered_map<uint32_t, UnseenAnimation> m_unseenAnimations;
    std::vector<ObjectChange> m_events; // the scripts' events since the last TakeObjectChanges
    std::unordered_map<uint32_t, uint32_t> m_created; // the created references that went out: id -> base
};
