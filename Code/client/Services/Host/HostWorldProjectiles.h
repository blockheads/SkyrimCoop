#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

struct ProjectileLaunchedEvent;
struct World;

// What the host's world objects fire (a trap's darts), sent to every guest to fire too (docs/design/traps.md).
struct HostWorldProjectiles
{
    HostWorldProjectiles(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    NO_COPY_MOVE(HostWorldProjectiles);

  private:
    void OnProjectileLaunched(const ProjectileLaunchedEvent& acEvent) const noexcept;
    World& m_world;
    std::vector<entt::scoped_connection> m_connections;
};
