#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <Structs/Perks.h>
#include <optional>
#include <unordered_map>
#include <vector>

struct TransportService;
class IActorBridge;
struct UpdateEvent;
struct DisconnectedEvent;
struct NotifyCharacterSpawn;
struct NotifyRemoveCharacter;

// Player perks remain personal; their copies hold the owner's ranks for native conditions and scripts.
struct PerkService
{
    PerkService(entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aBridge) noexcept;
    NO_COPY_MOVE(PerkService);

  private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnPerks(const NotifyActorPerks& acMessage) noexcept;
    void OnSpawn(const NotifyCharacterSpawn& acMessage) noexcept;
    void OnRemove(const NotifyRemoveCharacter& acMessage) noexcept;

    TransportService& m_transport;
    IActorBridge& m_bridge;
    std::optional<Perks> m_sent;
    struct Copy
    {
        Perks Content;
        uint32_t AppliedTo{};
    };
    std::unordered_map<uint32_t, Copy> m_copies;
    std::vector<entt::scoped_connection> m_connections;
};
