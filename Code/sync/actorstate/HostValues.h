#pragma once

#include <network/ENetInterface.h>
#include <Messages/RelayedMessages.h>

#include <NoCopyMove.h>

struct HostCharacters;
struct HostPlayers;
struct HostRouter;

// Actor values: a hit lands in its owner's game, which then reports the resulting values to the others. A player's
// character's values and perks also go into its record for its later spawns.
struct HostValues
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostValues(HostRouter& aRouter, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept;

    NO_COPY_MOVE(HostValues);

private:
    // The relayed requests, watched: into the record, which a read of the host's copy never overwrites
    // (docs/design/death.md, "Copies").
    void OnValues(const RequestActorValueChanges& acRequest, ConnectionId_t aSender) noexcept;
    void OnMaxValues(const RequestActorMaxValueChanges& acRequest, ConnectionId_t aSender) noexcept;
    void OnPerks(const ActorPerksRequest& acRequest, ConnectionId_t aSender) noexcept;

    const HostPlayers& m_players;
    HostCharacters& m_characters;
};
