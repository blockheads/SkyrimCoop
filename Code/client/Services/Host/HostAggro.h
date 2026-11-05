#pragma once
#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <Services/Host/PartyThreat.h>
#include <functional>

struct World;
struct ActorDamagedEvent;
struct UpdateEvent;
class IActorBridge;

struct HostAggro
{
    HostAggro(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors,
              std::function<void(const PartyThreat::State&)> aPublish,
              std::function<bool(uint32_t)> aCrimeHostile) noexcept;
    ~HostAggro() noexcept;
    NO_COPY_MOVE(HostAggro);
    void Update(const UpdateEvent&) noexcept;

  private:
    void OnDamage(const ActorDamagedEvent&) noexcept;
    void Clear() noexcept;
    World& m_world;
    IActorBridge& m_actors;
    PartyThreat::State m_state;
    std::function<void(const PartyThreat::State&)> m_publish;
    std::function<bool(uint32_t)> m_crimeHostile;
    std::vector<entt::scoped_connection> m_connections;
};
