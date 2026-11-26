#pragma once

#include <core/Listen.h>
#include <NoCopyMove.h>

#include <optional>

struct GameId;
struct TrapHitEvent;
struct World;

// The host's traps (docs/design/traps.md): its script's hit on a guest's copy goes to that guest's game.
struct HostTraps
{
    HostTraps(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    NO_COPY_MOVE(HostTraps);

  private:
    void OnTrapHit(const TrapHitEvent& acEvent) const noexcept;
    [[nodiscard]] std::optional<GameId> ToWire(uint32_t aFormId) const noexcept; // a created one by the host's id
    World& m_world;
    std::vector<entt::scoped_connection> m_connections;
};
