#pragma once

#include <Events/Listen.h>
#include <Messages/RelayedMessages.h>

#include <NoCopyMove.h>

#include <unordered_set>
#include <vector>

class IActorBridge;
struct ActionEvent;
struct CharacterService;
struct DisconnectedEvent;
struct PairKillEvent;
struct TransportService;
struct UpdateEvent;
struct World;

// Pairs claimed through the host (docs/design/one-simulator.md, piece 4b; docs/re/systems/kill-moves.md): a kill move
// this game's actor starts on another game's actor is claimed; an NPC partner is this game's for the pair, and goes
// back with what the pair did to it.
struct PairService
{
    PairService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;

    NO_COPY_MOVE(PairService);

private:
    void OnAction(const ActionEvent& acAction) noexcept;
    void OnPairKill(const PairKillEvent& acEvent) noexcept;
    void OnClaim(const NotifyPairClaim& acMessage) noexcept;
    void OnRefused(const NotifyPairRefused& acMessage) noexcept;
    void OnDone(const NotifyPairDone& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;

    // A pair one of this game's actors started on another game's actor.
    struct Own
    {
        uint32_t ActorId{}; // session ids
        uint32_t PartnerId{};
        uint32_t ActorFormId{};
        uint32_t PartnerFormId{};
        uint32_t ActionId{};
        uint64_t Tick{};
        bool Granted{};
        bool Lent{};   // the partner is this game's for the pair
        bool Killed{}; // its clip's KillActor came
        bool Ended{};  // the pair ended here; Since: the partner's graph updates recorded then
        uint64_t Since{};
        bool Sent{};
    };
    // A refused pair's plain swing, once the yielded clip has left.
    struct Swing
    {
        uint32_t FormId{};
        uint32_t ActionId{};
        uint64_t Until{};
    };
    // Hosting: an NPC a claimed pair's clip killed, handed back: taken once its copy here played its own KillActor
    // (its death then comes at the blow it shows), or cPoseHoldMs on (it never played the clip).
    struct Back
    {
        uint32_t PartnerId{}; // session ids
        uint32_t KillerId{};
        uint64_t Tick{};
        uint64_t Until{};
    };
    std::vector<Back> m_backs;
    std::unordered_set<uint32_t> m_killsSeen; // copies in a claimed pair whose clip's KillActor came (session ids)
    void SendDone(Own& aOwn, uint64_t aTick) noexcept;
    [[nodiscard]] entt::entity EntityOf(uint32_t aSessionId) const noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    std::vector<Own> m_own;
    std::vector<Swing> m_swings;
    std::vector<entt::scoped_connection> m_connections;
};
