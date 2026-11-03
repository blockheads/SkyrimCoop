#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

struct World;
struct UpdateEvent;
struct DisconnectedEvent;
class IActorBridge;
class IPlayerBridge;

struct PartyMarkerService
{
    PartyMarkerService(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors, IPlayerBridge& aPlayer) noexcept;
    ~PartyMarkerService() noexcept;
    NO_COPY_MOVE(PartyMarkerService);

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    World& m_world;
    IActorBridge& m_actors;
    IPlayerBridge& m_player;
    std::vector<entt::scoped_connection> m_connections;
};
