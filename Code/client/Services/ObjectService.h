#pragma once

#include <Events/Listen.h>

#include <unordered_set>

class IFormBridge;
class IActorBridge;

struct World;
struct ActivateEvent;
struct TransportService;
struct NotifyActivate;
struct LidUnposedEvent;
struct UpdateEvent;

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
    // A lid whose open state changed with no 3D: posed at its open state once it is enabled and loaded again
    // (docs/re/systems/guest-world-events.md, "A lid after a disable").
    void OnLidUnposed(const LidUnposedEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;

    World& m_world;
    TransportService& m_transport;
    IFormBridge& m_formBridge;
    IActorBridge& m_actorBridge;
    std::unordered_set<uint32_t> m_lidsToPose; // by form id

    std::vector<entt::scoped_connection> m_connections;
};
