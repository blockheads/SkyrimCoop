#pragma once

class IActorBridge;
class IFormBridge;
class IPlayerBridge;
struct World;
struct TransportService;
struct UpdateEvent;
struct ProjectileLaunchedEvent;
struct NotifyProjectileLaunch;
struct HitEvent;

/**
 * @brief Responsible for projectiles, combat agro, etc.
 */
struct CombatService
{
    CombatService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher, IFormBridge& aFormBridge,
                  IActorBridge& aActorBridge, IPlayerBridge& aPlayerBridge);
    ~CombatService() noexcept = default;

    NO_COPY_MOVE(CombatService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnLocalComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept;
    void OnProjectileLaunchedEvent(const ProjectileLaunchedEvent& acEvent) const noexcept;
    void OnNotifyProjectileLaunch(const NotifyProjectileLaunch& acMessage) const noexcept;
    void OnHitEvent(const HitEvent& acEvent) noexcept;
    void RunHitFeedback() noexcept;

private:
    World& m_world;
    TransportService& m_transport;
    IFormBridge& m_formBridge;
    IActorBridge& m_actorBridge;
    IPlayerBridge& m_playerBridge;

    // Another game's actors our player hit lately, with the health our copy showed then: the
    // owner's lower health arriving shows the health bar again (see OnHitEvent).
    struct RecentHit
    {
        std::chrono::steady_clock::time_point At;
        float Health;
    };
    std::unordered_map<uint32_t, RecentHit> m_recentHits;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_localComponentRemoved;
    entt::scoped_connection m_projectileLaunchedConnection;
    entt::scoped_connection m_projectileLaunchConnection;
    entt::scoped_connection m_hitConnection;
};
