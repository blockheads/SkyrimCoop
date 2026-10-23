#pragma once

#include <Events/Listen.h>
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

    ObjectService(const ObjectService&) = delete;
    ObjectService(ObjectService&&) = default;

private:
    void OnActivate(const ActivateEvent&) noexcept;
    void OnActivateNotify(const NotifyActivate&) noexcept;


    World& m_world;
    TransportService& m_transport;
    IFormBridge& m_formBridge;
    IActorBridge& m_actorBridge;

    std::vector<entt::scoped_connection> m_connections;
};
