#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <Structs/GameId.h>

#include <optional>

class IActorBridge;
class IFormBridge;
struct TrapContactEvent;
struct TrapHitEvent;
struct TransportService;
struct World;

/**
 * @brief The world's traps (docs/design/traps.md): what touched a guest's player goes to the host's script, its hit to
 * that player's game; what the host's objects fire (HostWorldProjectiles) is fired here too.
 */
struct TrapService
{
    TrapService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge,
                IFormBridge& aFormBridge) noexcept;

    NO_COPY_MOVE(TrapService);

private:
    void OnTrapContact(const TrapContactEvent& acEvent) const noexcept;
    void OnTrapHit(const TrapHitEvent& acEvent) const noexcept;
    void OnTrapHitNotify(const NotifyTrapHit& acMessage) const noexcept;
    void OnWorldProjectile(const NotifyWorldProjectile& acMessage) const noexcept;

    // A world reference's id on the wire and back, a created one's by the host's id.
    [[nodiscard]] std::optional<GameId> ToWire(uint32_t aFormId) const noexcept;
    [[nodiscard]] uint32_t FromWire(const GameId& acId) const noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IFormBridge& m_formBridge;
    std::vector<entt::scoped_connection> m_connections;
};
