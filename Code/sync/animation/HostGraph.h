#pragma once

#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>

struct HostCharacters;
struct HostPlayers;
struct HostRouter;

// The graph input stream (docs/design/graph-input-stream.md): ops from each actor's owner to the others; a copy's game
// asks the owner for a sync point.
struct HostGraph
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostGraph(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters) noexcept;
    NO_COPY_MOVE(HostGraph);

  private:
    // Each actor's run goes to the others if the sender owns that actor.
    void OnGraphOps(const GraphOpsRequest& acRequest, ConnectionId_t aSender) const noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
};
