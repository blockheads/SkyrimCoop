#pragma once

#include <Events/PacketEvent.h>

struct RequestSetWaypoint;
struct RequestRemoveWaypoint;

namespace Server
{

struct World;

/**
 * @brief Handles player specific actions that might change the information needed by other clients about that player.
 */
struct MapService
{
    MapService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~MapService() noexcept = default;

    TP_NOCOPYMOVE(MapService);

  protected:
    void OnSetWaypointRequest(const PacketEvent<RequestSetWaypoint>& acMessage) const noexcept;
    void OnRemoveWaypointRequest(const PacketEvent<RequestRemoveWaypoint>& acMessage) const noexcept;

  private:
    World& m_world;

    entt::scoped_connection m_playerSetWaypointConnection;
    entt::scoped_connection m_playerRemoveWaypointConnection;
    entt::scoped_connection m_updateConnection;
};

} // namespace Server
