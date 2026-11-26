#pragma once

#include <network/ENetInterface.h>
#include <NoCopyMove.h>
#include <Messages/Payloads.h>
#include <Messages/RelayedMessages.h>

#include <functional>
#include <unordered_map>
#include <unordered_set>

struct HostRouter;
struct HostCharacters;
struct HostCharactersGame;
struct HostPlayers;

// Action claims (one-simulator.md, "Action claims"): one game holds the actors an action involves, all or none; an
// actor is in one claim at a time. A taken member is the holder's game's until given back; a locked one keeps its owner.
struct HostClaims
{
    using ConnectionId_t = Net::ConnectionId_t;

    // A kind's own part (a ride's rider and seating, a kill move's victim able to pair).
    struct Rule
    {
        std::function<bool(const Payloads::Claim&)> Allows;
        // aOwnersChanged false for a claim the holder held already (a rider getting back on).
        std::function<void(const Payloads::Claim&, ConnectionId_t aTo, bool aOwnersChanged)> Granted;
        std::function<void(const Payloads::Claim&)> Ended;
    };

    // Registers its rules with aRouter.
    HostClaims(HostRouter& aRouter, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostCharactersGame& acGame) noexcept;

    NO_COPY_MOVE(HostClaims);

    void SetRule(uint8_t aKind, Rule aRule) noexcept { m_rules[aKind] = std::move(aRule); }
    // The holder's game of a claim on aActorId, or 0.
    [[nodiscard]] ConnectionId_t HolderOf(uint32_t aActorId) const noexcept;
    // Its claim passes to aTo's game for aForId, the same members (a passenger driving on). False if not claimed.
    bool PassOn(uint32_t aActorId, ConnectionId_t aTo, uint32_t aForId) noexcept;
    // aActorId's claim gives it back at once, with no holder's word (its rider gone).
    void Release(uint32_t aActorId) noexcept;
    // Given back, not yet taken by the host's game (its copy still running the holder's ops): claimed by nobody meanwhile.
    [[nodiscard]] bool Returning(uint32_t aActorId) const noexcept { return m_returning.contains(aActorId); }
    // The host's game took it back.
    void Taken(uint32_t aActorId) noexcept { m_returning.erase(aActorId); }
    // The live claims, to a game that just got their actors' spawns (a joiner's replays go by them).
    void SendClaims(ConnectionId_t aPlayer) const noexcept;
    // Its claims end, every member back.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;
    // The actor is gone: a claim it was in ends, its other members back.
    void Forget(uint32_t aActorId) noexcept;

private:
    void OnClaim(const ClaimRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnGiveBack(const ActorGiveBackRequest& acRequest, ConnectionId_t aSender) noexcept;
    void Refuse(const ClaimRequest& acRequest, uint8_t aReason) noexcept;
    void Grant(Payloads::Claim aClaim, ConnectionId_t aTo) noexcept;
    // One member back (the holder's give-back, or none from it); the claim ends with its last.
    void GiveBack(uint32_t aClaimId, uint32_t aActorId, const Payloads::ActorGiveBack& acGiveBack) noexcept;
    void End(uint32_t aClaimId) noexcept;
    // aTo's game may hold it: it is aTo's already, or the host's game could take it back (Claimable).
    [[nodiscard]] bool Takeable(uint32_t aActorId, ConnectionId_t aTo) const noexcept;
    void Ended(const Payloads::Claim& acClaim) noexcept;

    struct Held
    {
        Payloads::Claim Claim;
        ConnectionId_t To{};
    };

    const HostPlayers& m_players;
    HostCharacters& m_characters;
    const HostCharactersGame& m_game;
    std::unordered_map<uint8_t, Rule> m_rules;
    std::unordered_map<uint32_t, Held> m_claims;      // by claim id
    std::unordered_map<uint32_t, uint32_t> m_inClaim; // member -> claim id
    std::unordered_set<uint32_t> m_returning;
    uint32_t m_lastClaimId = 0;
};
