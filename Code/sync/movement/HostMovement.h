#pragma once

#include <network/ENetInterface.h>

#include <NoCopyMove.h>

struct HostCharacters;
struct HostNetwork;
struct HostPlayers;
struct HostRouter;
struct ReferencesMoveRequest;

// Owners' movement goes straight to everyone else (unreliable: only the latest matters); a player's character's
// keeps its record's place current.
struct HostMovement
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rule with aRouter.
    HostMovement(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept;

    NO_COPY_MOVE(HostMovement);

private:
    void OnMovement(const ReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    HostCharacters& m_characters;
};
