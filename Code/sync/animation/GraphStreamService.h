#pragma once

#include <core/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

class IActorBridge;

struct World;
struct TransportService;
struct ActionEvent;

/**
 * @brief An owner's graph input stream to its copies (docs/design/graph-input-stream.md), and its actions.
 *
 * The owner's ops go out each send round (Send); a copy is fed its owner's, asks for a sync point, and loads it.
 * An owner's own action (ActionEvent) is queued for its next movement snapshot.
 */
struct GraphStreamService
{
    GraphStreamService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;
    ~GraphStreamService() noexcept = default;

    NO_COPY_MOVE(GraphStreamService);

    // This round's ops and sync points of this game's actors, to the host (MovementService's send round).
    void Send() const noexcept;

    void OnActionEvent(const ActionEvent& acActionEvent) const noexcept;
    // An actor this game no longer owns, or no longer has a copy of: its graph stream ends.
    void OnStreamedRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept;
    // An owner's ops for its copies here; a copy asking this game, its owner, for a sync point; the owner's sync
    // point for a copy here.
    void OnGraphOps(const NotifyGraphOps& acMessage) const noexcept;
    void OnGraphSync(const NotifyGraphSync& acMessage) const noexcept;
    void OnGraphSyncPoint(const NotifyGraphSyncPoint& acMessage) const noexcept;

private:
    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;

    std::vector<entt::scoped_connection> m_connections;
};
