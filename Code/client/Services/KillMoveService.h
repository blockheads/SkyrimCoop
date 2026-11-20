#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>

#include <NoCopyMove.h>

#include <vector>

class IActorBridge;
struct ActionEvent;
struct DisconnectedEvent;
struct TransportService;
struct UpdateEvent;
struct World;

// A kill move this game's actor starts on another game's actor, as an action claim (one-simulator.md, "Action claims";
// docs/re/systems/kill-moves.md): its attacker locked, its NPC victim taken (its death kept for the give-back) or its
// player victim locked. A refusal ends the pair here; its end gives every member back.
struct KillMoveService
{
    KillMoveService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;

    NO_COPY_MOVE(KillMoveService);

private:
    void OnAction(const ActionEvent& acAction) noexcept;
    void OnClaim(const NotifyClaim& acMessage) noexcept;
    void OnRefused(const NotifyClaimRefused& acMessage) noexcept;
    void OnGiveBack(const NotifyActorGiveBack& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;

    // A kill move one of this game's actors started on another game's actor.
    struct Own
    {
        uint32_t ActorId{}; // session ids
        uint32_t PartnerId{};
        uint32_t ActorFormId{};
        uint32_t PartnerFormId{};
        uint32_t ActionId{};
        uint64_t Tick{};
        uint32_t ClaimId{};
        bool Granted{};
        bool Taken{}; // its victim taken by this game
        bool Sent{};
    };
    // A refused kill move's plain swing, once the yielded clip has left.
    struct Swing
    {
        uint32_t FormId{};
        uint32_t ActionId{};
        uint64_t Until{};
    };
    void SendDone(Own& aOwn) noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    std::vector<Own> m_own;
    std::vector<Swing> m_swings;
    std::vector<entt::scoped_connection> m_connections;
};
