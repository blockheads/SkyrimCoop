#pragma once

#include <network/ENetInterface.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

#include <vector>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostPlayers;

/**
 * @brief Pairs (kill moves) claimed through the host (docs/design/one-simulator.md, piece 4b): the first claim naming
 * either actor holds until its claimer says the pair ended. A host NPC partner is claimed for the claimer's game
 * (HostClaims, given back as any claim); a player partner stays its own game's. Any other claim on either actor is
 * refused.
 */
struct HostPairs
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aAuthority.
    HostPairs(HostAuthority& aAuthority, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostPairs);

    // Its pairs end (before its characters go): the other games hear it.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    void OnClaim(const PairClaimRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnDone(const PairDoneRequest& acRequest, ConnectionId_t aSender) noexcept;
    [[nodiscard]] bool Claimed(uint32_t aSessionId) const noexcept;

    struct Claim
    {
        uint32_t ActorId{};
        uint32_t PartnerId{};
        ConnectionId_t Claimer{};
    };

    const HostPlayers& m_players;
    HostCharacters& m_characters;
    const HostGame& m_game;
    std::vector<Claim> m_claims;
};
