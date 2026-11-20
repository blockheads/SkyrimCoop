#pragma once

#include <Events/Listen.h>
#include <Messages/Payloads.h>
#include <Messages/RelayedMessages.h>

#include <NoCopyMove.h>

#include <functional>
#include <unordered_map>
#include <vector>

class IActorBridge;
struct DisconnectedEvent;
struct TransportService;
struct UpdateEvent;
struct World;

// Action claims on every game (HostClaims; one-simulator.md, "Action claims"): the holder's game takes a claim's taken
// members, the others give them up; the holder gives each back; every game keeps the answers (ActionClaims).
struct ClaimService
{
    ClaimService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;

    NO_COPY_MOVE(ClaimService);

    // This game holds the actor (session id) through a claim, from its NotifyClaim to its give-back.
    [[nodiscard]] bool Holds(uint32_t aSessionId) const noexcept;
    // Gives it back at the next update after its next recorded graph update (aNow: at once), its ops first, then
    // aBefore (the feature's own message). Killed: aKilled, or a kill its hold counted by then.
    void GiveBack(uint32_t aSessionId, bool aKilled, bool aNow, std::function<void()> aBefore = {}) noexcept;

private:
    void OnClaim(const NotifyClaim& acMessage) noexcept;
    void OnRefused(const NotifyClaimRefused& acMessage) noexcept;
    void TakeMember(const Payloads::ClaimMember& acMember, const NotifyClaim& acClaim) noexcept;
    void OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;

    struct Held
    {
        uint32_t ClaimId{};
        bool Took{}; // a copy here until the claim: given up again with its give-back
    };
    // A give-back names the claim held when it was asked; one asked before its claim was heard (a dismount before the
    // ride's answer) takes the next claim heard for it, or is dropped by Expires (the ride refused).
    struct Pending
    {
        uint32_t SessionId{};
        uint32_t ClaimId{};
        uint64_t Expires{};
        uint32_t FormId{}; // 0: not this game's (its claim not heard yet)
        uint64_t Since{};  // its graph updates recorded when asked
        bool Killed{};
        bool Now{};
        bool Waited{}; // an update has passed since it was asked
        std::function<void()> Before;
    };
    void RunGiveBacks() noexcept;
    [[nodiscard]] entt::entity EntityOf(uint32_t aSessionId) const noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    std::unordered_map<uint32_t, Held> m_held;
    std::vector<Pending> m_giveBacks;
    std::vector<entt::scoped_connection> m_connections;
};
