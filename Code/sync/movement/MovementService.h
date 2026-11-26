#pragma once

#include <core/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

#include <unordered_set>

class IActorBridge;
class IPlayerBridge;

struct World;
struct TransportService;
struct NotifyReferencesMove;
struct ReferenceJumpEvent;

/**
 * @brief Where actors are: this game's actors' snapshots out, their copies played back
 * (docs/design/copy-clock.md, docs/re/systems/actor-movement.md).
 *
 * CharacterService's frame calls both rounds in order (RunLocalUpdates, then RunRemoteUpdates).
 */
struct MovementService
{
    MovementService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge,
                    IPlayerBridge& aPlayerBridge) noexcept;
    ~MovementService() noexcept = default;

    NO_COPY_MOVE(MovementService);

    // The send round: this game's actors' snapshots and graph streams, when one is due.
    void RunLocalUpdates() noexcept;
    // Each copy stepped to its owner's place at its playback tick, then its animation.
    void RunRemoteUpdates() noexcept;
    // A copy with no two snapshots yet: at its owner's last place, not its last goal.
    void HoldCopy(entt::entity aEntity, uint32_t aFormId) const noexcept;

    void OnReferencesMoveRequest(const NotifyReferencesMove& acMessage) const noexcept;
    // An actor of ours put somewhere at once: its next snapshot says so (Movement::Teleported).
    void OnReferenceJump(const ReferenceJumpEvent& acEvent) noexcept;

private:
    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IPlayerBridge& m_playerBridge;
    std::unordered_set<uint32_t> m_jumped; // form ids, until their next snapshot
    uint64_t m_lastSnapshot{}; // the last send round's frame tick
    uint64_t m_round{};        // send rounds so far (each actor's last: LocalComponent::LastSentRound)

    std::vector<entt::scoped_connection> m_connections;
};
