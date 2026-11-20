#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>
#include <Structs/ActorWhereabouts.h>

#include <NoCopyMove.h>

#include <functional>
#include <unordered_set>
#include <vector>

class IActorBridge;
struct DisconnectedEvent;
struct TransportService;
struct UpdateEvent;
struct World;

// Claims on the host's actors on every game (HostClaims; docs/design/one-simulator.md, R4): the holder's game takes
// a claimed actor, the others give it up; given back, the host's game takes it once its copy ran the holder's ops.
struct ClaimService
{
    ClaimService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;

    NO_COPY_MOVE(ClaimService);

    // This game holds the actor (session id) through a claim, from its NotifyActorClaim to its give-back.
    [[nodiscard]] bool Holds(uint32_t aSessionId) const noexcept;
    // Gives back an actor this game holds (or asked for: the host drops one from a game not holding it): after its
    // next recorded graph update (its last ops, an exit pair's PairEnd, due with it), or at once (aNow: a killed one's
    // graph is stopped). Its graph ops go first, then aBefore (the feature's own message), then the give-back.
    void GiveBack(uint32_t aSessionId, bool aKilled, bool aNow, std::function<void()> aBefore = {}) noexcept;

private:
    void OnClaim(const NotifyActorClaim& acMessage) noexcept;
    void OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;

    struct Pending
    {
        uint32_t SessionId{};
        uint32_t FormId{}; // 0: not this game's (its claim not heard yet)
        uint64_t Since{};  // its graph updates recorded when asked
        bool Killed{};
        bool Now{};
        std::function<void()> Before;
    };
    // Hosting: an actor given back, taken once its copy ran the holder's ops through Tick (TakesOverMount); a killed
    // one once its copy counted its own clip's kill too (its death then comes at the blow it shows), or by Until.
    struct TakeOver
    {
        uint32_t SessionId{};
        uint64_t Tick{};
        ActorWhereabouts Whereabouts{};
        uint32_t KillerId{}; // session id, 0: lives
        uint64_t Until{};
    };
    void RunGiveBacks() noexcept;
    [[nodiscard]] bool Take(const TakeOver& acTakeOver, bool aNow) noexcept;
    void TakeNow(uint32_t aSessionId) noexcept;
    [[nodiscard]] entt::entity EntityOf(uint32_t aSessionId) const noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    std::unordered_set<uint32_t> m_held;
    std::vector<Pending> m_giveBacks;
    std::vector<TakeOver> m_takeOvers;
    std::vector<entt::scoped_connection> m_connections;
};
