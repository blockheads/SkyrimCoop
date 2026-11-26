#pragma once

#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>

struct ActivateRequest;
struct ContainerChangeRequest;
struct HostCharacters;
struct HostWorldGame;
struct HostPlayers;
struct HostRouter;
struct LockChangeRequest;

// A guest's doors, levers, locks and containers: proposals the host's world takes. The host's own go out as its
// object changes (HostWorldState).
struct HostObjects
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostObjects(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostWorldGame& acGame) noexcept;
    NO_COPY_MOVE(HostObjects);

  private:
    // The host's game runs a guest's activation with its copy; the others get what changed as its rows.
    void OnActivate(const ActivateRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A guest's player picked a lock, or used a key.
    void OnLockChange(const LockChangeRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A guest's player took or stored an item.
    void OnContainerChange(const ContainerChangeRequest& acRequest, ConnectionId_t aSender) const noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostWorldGame& m_game;
};
