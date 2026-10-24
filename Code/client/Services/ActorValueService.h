#pragma once

#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>
#include <Structs/ActorValues.h>
#include <map>
#include <optional>
#include <set>

struct World;
struct DisconnectedEvent;
struct ActorRemovedEvent;
struct UpdateEvent;
struct HealthChangeEvent;
struct ActorValueChangeEvent;
struct ActorDeathChangeEvent;
struct DeathActionReplayedEvent;
struct ReferenceLoadedEvent;
struct NotifyCharacterSpawn;
struct NotifyRemoveCharacter;
struct TransportService;
class IActorBridge;

// Owner writes send discrete values and death; regenerating pools are compared every 100 ms.
struct ActorValueService
{
    ActorValueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~ActorValueService() noexcept = default;
    NO_COPY_MOVE(ActorValueService);

private:
    void OnLocalComponentAdded(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnActorRemoved(const ActorRemovedEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnValueChanged(const ActorValueChangeEvent&) noexcept;
    void OnDeathChanged(const ActorDeathChangeEvent&) noexcept;
    void OnDeathActionReplayed(const DeathActionReplayedEvent&) noexcept;
    void OnReferenceLoaded(const ReferenceLoadedEvent&) noexcept;
    void OnSpawn(const NotifyCharacterSpawn&) noexcept;
    void OnRemove(const NotifyRemoveCharacter&) noexcept;
    void OnActorValueChanges(const NotifyActorValueChanges&) noexcept;
    void OnActorMaxValueChanges(const NotifyActorMaxValueChanges&) noexcept;
    void OnHealthChange(const HealthChangeEvent&) noexcept;
    void OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast&) const noexcept;
    void OnDeathStateChange(const NotifyDeathStateChange&) noexcept;
    void ApplyDeathState(uint32_t aSessionId) noexcept;
    void ApplyCopyPools() noexcept;
    void RunSmallHealthUpdates() noexcept;
    void RunPoolUpdates() noexcept;
    void RunDeathStateUpdates() noexcept;
    void BroadcastActorValues() noexcept;
    void CreateActorValuesComponent(entt::entity aEntity, uint32_t aFormId) noexcept;

    struct CopyDeath
    {
        bool IsDead;
        std::optional<uint64_t> ActionTick;
    };

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    std::unordered_map<uint32_t, float> m_smallHealthChanges;
    std::map<uint32_t, std::set<uint32_t>> m_changedValues;
    std::map<uint32_t, bool> m_changedDeaths; // form id -> final engine event seen
    std::map<uint32_t, std::optional<uint64_t>> m_resurrections;
    // Kept after convergence too: a delayed death action must not undo a newer resurrection.
    std::unordered_map<uint32_t, CopyDeath> m_copyDeathStates;
    std::unordered_map<uint32_t, uint64_t> m_replayedDeaths; // form id -> owner action tick
    std::unordered_map<uint32_t, ActorValues> m_copyValues; // session id -> the owner's values
    std::set<uint32_t> m_copyPoolsPending;
    std::chrono::steady_clock::time_point m_lastPoolSend;
};
