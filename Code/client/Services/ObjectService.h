#pragma once

class IFormBridge;
class IActorBridge;

struct World;
struct ActivateEvent;
struct TransportService;
struct NotifyActivate;

/**
 * @brief Handles objects in the environment.
 */
struct ObjectService
{
    ObjectService(World&, entt::dispatcher&, TransportService&, IFormBridge&, IActorBridge&);

private:
    void OnActivate(const ActivateEvent&) noexcept;
    void OnActivateNotify(const NotifyActivate&) noexcept;


    World& m_world;
    TransportService& m_transport;
    IFormBridge& m_formBridge;
    IActorBridge& m_actorBridge;

    entt::scoped_connection m_onActivateConnection;
    entt::scoped_connection m_activateConnection;
};
