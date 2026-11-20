#pragma once

#include <network/ENetInterface.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

#include <unordered_map>

struct HostAuthority;
struct HostCharacters;
struct HostPlayers;

/**
 * @brief Claims on the host's actors (docs/design/one-simulator.md, R4): one game holds a claimed actor, as its owner,
 * until it gives it back. The feature rules decide when to claim (HostCharacters::OnMount, HostPairs); a give-back is
 * the same for every one: ActorGiveBackRequest, the host's game taking it once its copy ran the holder's ops.
 */
struct HostClaims
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aAuthority.
    HostClaims(HostAuthority& aAuthority, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept;

    NO_COPY_MOVE(HostClaims);

    // aTo's game holds aActorId for aForId (its rider, a kill move's attacker). False if another game holds it (unless
    // aTakeFrom: a passenger driving on) or it is a player's. Every game hears of a new holder.
    bool Claim(uint32_t aActorId, ConnectionId_t aTo, uint32_t aForId, bool aDeathOnGiveBack, bool aTakeFrom = false) noexcept;
    // The holder's game, or 0.
    [[nodiscard]] ConnectionId_t HolderOf(uint32_t aActorId) const noexcept;
    // The host's again at once, with no holder's word (its rider gone).
    void Release(uint32_t aActorId) noexcept;
    // Whatever it still holds goes back to the host.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;
    // The actor is gone.
    void Forget(uint32_t aActorId) noexcept { m_holds.erase(aActorId); }

private:
    void OnGiveBack(const ActorGiveBackRequest& acRequest, ConnectionId_t aSender) noexcept;
    void GiveBack(uint32_t aActorId, const Payloads::ActorGiveBack& acGiveBack) noexcept;

    struct Hold
    {
        ConnectionId_t To{};
        uint32_t ForId{};
    };

    const HostPlayers& m_players;
    HostCharacters& m_characters;
    std::unordered_map<uint32_t, Hold> m_holds;
};
