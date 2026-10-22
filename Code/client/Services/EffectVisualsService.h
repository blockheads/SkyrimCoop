#pragma once

#include <chrono>

class IActorBridge;

struct World;
struct TransportService;
struct UpdateEvent;
struct NotifyActiveEffects;

/**
 * @brief What magic effects show on actors (burning, frost, a healing glow), as shared state.
 *
 * An actor's owner applies its effects; copies never do (docs/design/copy-contract.md). The owner
 * shares which effects with visuals are active on its actors (RequestActiveEffects: within 250ms
 * of a change, and every 2s while any are). Each copy plays only their hit visuals, from when an
 * effect appears in its owner's list until it leaves it, like Unreal's gameplay cues. A copy that
 * loads later, or misses a message, converges with the next one; one whose owner goes quiet
 * stops them.
 */
struct EffectVisualsService
{
    EffectVisualsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;
    ~EffectVisualsService() noexcept = default;

    TP_NOCOPYMOVE(EffectVisualsService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnNotifyActiveEffects(const NotifyActiveEffects& acMessage) noexcept;
    void OnRemoteComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;

private:
    using Clock = std::chrono::steady_clock;

    void SendOwnedEffects() noexcept;
    // Plays what copies miss; stops copies whose owner went quiet.
    void RunCopies() noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;

    Clock::time_point m_lastCheck{};
    Clock::time_point m_lastFullSend{};
    // Per owned actor (session id): the effect form ids last sent.
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_sent;
    // Per copy (form id): the effects its owner lists (whose visuals it plays), and when it last
    // heard from the owner.
    struct Shown
    {
        std::vector<uint32_t> EffectIds;
        Clock::time_point LastHeard;
    };
    std::unordered_map<uint32_t, Shown> m_shown;

    void PlayMissing(uint32_t aFormId, const Shown& acShown) noexcept;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_notifyConnection;
    entt::scoped_connection m_remoteRemovedConnection;
};
