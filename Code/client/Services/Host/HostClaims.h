#pragma once

#include <network/ENetInterface.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

#include <functional>
#include <unordered_map>
#include <unordered_set>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostPlayers;

// Claims on the host's actors: one game holds one, as its owner, until it gives it back (one-simulator.md, "One claim
// for every actor"). The features decide when to claim; the give-back is the same for all.
struct HostClaims
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aAuthority.
    HostClaims(HostAuthority& aAuthority, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostClaims);

    // aTo's game holds aActorId for aForId (its rider, a kill move's attacker). False if it is held for anything else
    // (unless aTakeFrom: a passenger driving on), on its way back to the host's game, a player's, or not Claimable.
    bool Claim(uint32_t aActorId, ConnectionId_t aTo, uint32_t aForId, bool aDeathOnGiveBack, bool aTakeFrom = false) noexcept;
    // The holder's game, or 0.
    [[nodiscard]] ConnectionId_t HolderOf(uint32_t aActorId) const noexcept;
    // The host's again at once, with no holder's word (its rider gone).
    void Release(uint32_t aActorId) noexcept;
    // Whatever it still holds goes back to the host.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;
    // Given back, not yet taken by the host's game (its copy still running the holder's ops): claimed by nobody meanwhile.
    [[nodiscard]] bool Returning(uint32_t aActorId) const noexcept { return m_returning.contains(aActorId); }
    // The host's game took it back.
    void Taken(uint32_t aActorId) noexcept { m_returning.erase(aActorId); }
    // The actor is gone.
    void Forget(uint32_t aActorId) noexcept;
    // A claim ended (given back, or its holder gone): the feature that made it ends its part (a ride still marked).
    std::function<void(uint32_t aActorId, uint32_t aForId)> OnEnded;

private:
    void OnGiveBack(const ActorGiveBackRequest& acRequest, ConnectionId_t aSender) noexcept;
    void GiveBack(uint32_t aActorId, const Payloads::ActorGiveBack& acGiveBack) noexcept;

    struct Hold
    {
        ConnectionId_t To{};
        uint32_t ForId{};
        uint32_t ClaimId{};
        bool DeathOnGiveBack{};
    };

    const HostPlayers& m_players;
    HostCharacters& m_characters;
    const HostGame& m_game;
    std::unordered_map<uint32_t, Hold> m_holds;
    std::unordered_set<uint32_t> m_returning;
    uint32_t m_lastClaimId = 0;
};
