#pragma once

class IFormBridge;
class IEventBridge;
class IActorBridge;

struct World;
struct ActivateEvent;
struct TransportService;
struct NotifyActivate;
struct LockChangeEvent;
struct NotifyLockChange;

/**
 * @brief Handles objects in the environment.
 */
struct ObjectService
{
    ObjectService(World&, entt::dispatcher&, TransportService&, IFormBridge&, IActorBridge&, IEventBridge&);

private:
    void OnActivate(const ActivateEvent&) noexcept;
    void OnActivateNotify(const NotifyActivate&) noexcept;
    void OnLockChange(const LockChangeEvent&) noexcept;
    void OnLockChangeNotify(const NotifyLockChange&) noexcept;


    World& m_world;
    TransportService& m_transport;
    IFormBridge& m_formBridge;
    IActorBridge& m_actorBridge;
    IEventBridge& m_eventBridge;

    entt::scoped_connection m_onActivateConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_lockChangeConnection;
    entt::scoped_connection m_lockChangeNotifyConnection;
};
