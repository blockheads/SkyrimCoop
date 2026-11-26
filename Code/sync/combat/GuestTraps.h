#pragma once

#include <core/Listen.h>
#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <Structs/GameId.h>

#include <optional>

struct TrapContactEvent;
struct World;

// A guest's traps (docs/design/traps.md): what touched its player goes to the host's script, the host's hit on it is
// taken here.
struct GuestTraps
{
    GuestTraps(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    NO_COPY_MOVE(GuestTraps);

  private:
    void OnTrapContact(const TrapContactEvent& acEvent) const noexcept;
    void OnTrapHit(const NotifyTrapHit& acMessage) const noexcept;
    [[nodiscard]] std::optional<GameId> ToWire(uint32_t aFormId) const noexcept; // a created one by the host's id
    [[nodiscard]] uint32_t FromWire(const GameId& acId) const noexcept;
    World& m_world;
    std::vector<entt::scoped_connection> m_connections;
};
