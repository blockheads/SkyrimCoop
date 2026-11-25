#pragma once

#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>

struct HostCharacters;
struct HostGame;
struct HostPlayers;
struct HostRouter;

// Blows between games: a player's stagger on another's actor, and traps (docs/design/traps.md).
struct HostCombat
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostCombat(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept;
    NO_COPY_MOVE(HostCombat);

  private:
    void OnMeleeStagger(const MeleeStaggerRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A trap touched a guest's player, from its own game; a trap's hit on a guest's player, from the host's.
    void OnTrapContact(const TrapContactRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void OnTrapHit(const TrapHitRequest& acRequest, ConnectionId_t aSender) const noexcept;

    const HostRouter& m_router;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
