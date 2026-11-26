#pragma once

#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>

struct HostCharacters;
struct HostMagicGame;
struct HostPlayers;
struct HostRouter;
struct SummonRequest;

// Spells and shots between games: most pass on from the actor's owner; a guest's summon the host's game makes.
struct HostMagic
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostMagic(HostRouter& aRouter, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostMagicGame& acGame) noexcept;
    NO_COPY_MOVE(HostMagic);

  private:
    // A guest's player cast a summon on itself: the host's game puts it on that player's copy (the host creates).
    void OnSummon(const SummonRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // An actor's shot from its owner; an object's (shooter 0: a dart port, a script's activator, a statue), of any
    // kind, from the game that ran it.
    void OnProjectileLaunch(const ProjectileLaunchRequest& acRequest, ConnectionId_t aSender) const noexcept;

    const HostRouter& m_router;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostMagicGame& m_game;
};
