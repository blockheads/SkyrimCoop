#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

struct World;
struct TransportService;

struct SetWaypointEvent;
struct RemoveWaypointEvent;
struct NotifySetWaypoint;
struct NotifyRemoveWaypoint;
class IPlayerBridge;

/**
 * @brief Handles logic related to the local player.
 */
struct MapService
{
    MapService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~MapService() noexcept = default;

    NO_COPY_MOVE(MapService);

  protected:
    void OnSetWaypoint(const SetWaypointEvent& acMessage) noexcept;
    void OnRemoveWaypoint(const RemoveWaypointEvent& acMessage) noexcept;
    void OnNotifySetWaypoint(const NotifySetWaypoint& acMessage) noexcept;
    void OnNotifyRemoveWaypoint(const NotifyRemoveWaypoint& acMessage) noexcept;

  private:
    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IPlayerBridge& m_playerBridge;

    std::vector<entt::scoped_connection> m_connections;
};
