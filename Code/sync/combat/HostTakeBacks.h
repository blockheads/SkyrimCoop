#pragma once

#include <core/Listen.h>
#include <Messages/RelayedMessages.h>
#include <Structs/ActorWhereabouts.h>

#include <NoCopyMove.h>

#include <functional>
#include <vector>

class IActorBridge;
struct UpdateEvent;
struct World;

// The host's game takes back a claimed actor given back (HostClaims): after its copy's replay, placed where its holder
// left it, then killed if it was (one-simulator.md, "One claim for every actor").
struct HostTakeBacks
{
    // aTaken: the host's rules hear it (HostAuthority::ActorTaken).
    HostTakeBacks(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors, std::function<void(uint32_t)> aTaken) noexcept;

    NO_COPY_MOVE(HostTakeBacks);

    void Update(const UpdateEvent&) noexcept;

private:
    void OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept;

    struct TakeBack
    {
        uint32_t SessionId{};
        uint64_t Tick{};
        ActorWhereabouts Whereabouts{};
        uint32_t KillerId{}; // session id, 0: lives
        uint64_t Until{};     // a killed one: by then even if its copy never counted its own clip's kill
        uint64_t StalledAt{}; // by then even if its copy's stream stopped short of Tick
    };
    [[nodiscard]] bool Take(const TakeBack& acTakeBack) noexcept;

    World& m_world;
    IActorBridge& m_actors;
    std::function<void(uint32_t)> m_taken;
    std::vector<TakeBack> m_takeBacks;
    std::vector<entt::scoped_connection> m_connections;
};
