#pragma once

#include <network/ENetInterface.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Structs/ActorWhereabouts.h>
#include <Messages/RelayedMessages.h>
#include <characters/HostClaims.h>
#include <mounts/HostRides.h>
#include <travel/HostArrivals.h>

#include <functional>
#include <optional>
#include <NoCopyMove.h>

struct HostRouter;
struct HostCharactersGame;
struct HostPlayers;
struct AnnounceCharacterRequest;
struct RequestRemoveCharacter;
struct RequestRespawn;

struct HostNetwork;

/**
 * @brief The session's characters on the host: hands out their session ids, knows who owns each,
 * and spawns them for players who did not see them appear.
 *
 * Players' characters keep a record of what the host's copy cannot show (appearance, base, face),
 * kept current with their movement; the host's own actors are read from its engine. Its rides are HostRides', where
 * each player's game is HostArrivals'.
 */
struct HostCharacters
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostCharacters(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharactersGame& acGame) noexcept;

    NO_COPY_MOVE(HostCharacters);

    // What the rest of the host's rules hear from here and answer, set by HostAuthority.
    struct Listeners
    {
        // A player's character is in the host's world; then, once, everything a joiner must know.
        std::function<void(ConnectionId_t aPlayer, uint32_t aId)> PlayerBodyEntered;
        std::function<void(ConnectionId_t aPlayer, uint32_t aId)> PlayerEntered;
        std::function<void(uint32_t aId, const RecordParts& acRecord)> ActorAnnounced;
        std::function<void(uint32_t aId)> ActorRemoved;
        // A character's record as its owner last declared it.
        std::function<RecordParts(uint32_t aId)> RecordOf;
    };
    // The host's engine renamed an actor another game owns: its owner names its actor, and its name part carries
    // that on.
    void NameActor(uint32_t aSessionId, const std::string& acName) const noexcept;
    void SetListeners(Listeners aListeners) noexcept { m_listeners = std::move(aListeners); }

    // The owner's connection (0: no such character).
    [[nodiscard]] ConnectionId_t OwnerOf(uint32_t aSessionId) const noexcept;
    // A player's character's record (kept for its later spawns) when aOwner owns it; else null.
    [[nodiscard]] NotifyCharacterSpawn* PlayerRecordOf(uint32_t aSessionId, ConnectionId_t aOwner) noexcept;
    // A session id for one of the host's actors, which its game announces under it.
    [[nodiscard]] uint32_t NewCharacterId() noexcept;
    // A joining player's character's session id (in its join answer), which only its game may announce.
    [[nodiscard]] uint32_t ReservePlayerCharacter(ConnectionId_t aPlayer) noexcept;
    // The player's own character (its session id), or none.
    [[nodiscard]] uint32_t PlayerCharacterOf(ConnectionId_t aPlayer) const noexcept;
    // One of the players' characters.
    [[nodiscard]] bool IsPlayerCharacter(uint32_t aSessionId) const noexcept;
    // A character's seats (a mount's rider and passengers), or null.
    [[nodiscard]] RideSeats* SeatsOf(uint32_t aSessionId) noexcept;
    // Each character in the table's order: its owner, its player record (null for none) and its seats.
    using Visit = std::function<void(uint32_t aSessionId, ConnectionId_t aOwner, NotifyCharacterSpawn* apRecord, RideSeats& aSeats)>;
    void ForEachCharacter(const Visit& acVisit) noexcept;
    // Every character but aPlayer's own (aOwnSessionId), as it is now.
    void SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnSessionId) noexcept;
    // Action claims (a ride, a kill move), which change their taken members' owner (SetOwner).
    [[nodiscard]] HostClaims& Claims() noexcept { return m_claims; }
    [[nodiscard]] const HostRides& Rides() const noexcept { return m_rides; }
    // Where each player's game is (the party tether).
    [[nodiscard]] HostArrivals& Arrivals() noexcept { return m_arrivals; }
    [[nodiscard]] const HostArrivals& Arrivals() const noexcept { return m_arrivals; }
    // A player's arrival where it was brought: the players' copies either way were made where it joined and may be
    // gone (a copy moved out of the loaded cells is), so each is sent again; a game that still has one refreshes it.
    void SendPlayersMet(ConnectionId_t aPlayer) noexcept;
    // The host's actors (not its player, whose copy travels with the party) to one player.
    void SendHostActors(ConnectionId_t aPlayer) noexcept;
    // A spawn of the character as it is now: its record as its owner last declared it.
    [[nodiscard]] std::optional<NotifyCharacterSpawn> BuildSpawn(uint32_t aSessionId) const noexcept;
    // HostClaims only: aTo's game owns the actor from now. False if it did already, or no such character.
    bool SetOwner(uint32_t aSessionId, ConnectionId_t aTo) noexcept;
    // Its characters leave with it.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    // A player's own character, or (the host only) one of the host's actors, under the id the host gave it:
    // everyone else gets a spawn.
    void OnAnnounceCharacter(const AnnounceCharacterRequest& acRequest, ConnectionId_t aSender) noexcept;
    // The owner stopped simulating the actor; its copies go where it went.
    void OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept;
    // The owner's looks update its existing copies; a revival remakes them. Anyone else lost its copy and asks
    // for a fresh spawn.
    void OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept;
    // The owner's actor's effects with visuals: to everyone else, and into a player's record for its later spawns.
    void OnActiveEffects(const RequestActiveEffects& acRequest, ConnectionId_t aSender) noexcept;
    // The host's game's spawns of its actors but aExcludedSessionId, to aPlayer. How many went.
    size_t SendHostSpawns(ConnectionId_t aPlayer, uint32_t aExcludedSessionId) noexcept;
    void SendToOthersWithHost(const NotifyCharacterSpawn& acSpawn, ConnectionId_t aSender) const noexcept;

    // A spawn as BuildSpawn without its record: the host's actor from its engine, a player's from its record and
    // the host's copy.
    [[nodiscard]] std::optional<NotifyCharacterSpawn> ReadSpawn(uint32_t aSessionId) const noexcept;
    // Its copies follow aWhereabouts (where the owner's actor went).
    void RemoveCharacter(uint32_t aSessionId, const ActorWhereabouts& acWhereabouts = {}) noexcept;

    struct Character
    {
        ConnectionId_t Owner{};
        // A player's: what is needed to spawn it again, kept current with its movement.
        std::optional<NotifyCharacterSpawn> PlayerRecord;
        // A mount's: who rides it (HostRides).
        RideSeats Seats;
        // Not a player's: the effects its owner last declared (a guest's horse), for a later spawn.
        std::optional<Payloads::ActiveEffects> Effects;
    };

    Listeners m_listeners;
    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharactersGame& m_game;
    std::unordered_map<uint32_t, Character> m_characters;
    // Never reused in a session: a late message for a removed character finds nothing.
    uint32_t m_lastSessionId = 0;
    HostClaims m_claims;
    HostRides m_rides;
    HostArrivals m_arrivals;
    // Each player's character's id, from its join.
    std::unordered_map<ConnectionId_t, uint32_t> m_playerCharacters;
};
