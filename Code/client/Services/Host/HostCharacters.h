#pragma once

#include <network/ENetInterface.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyPartyTravel.h>
#include <Structs/ActorWhereabouts.h>

#include <optional>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostGame;
struct HostPlayers;
struct ReferencesMoveRequest;
struct AssignCharacterRequest;
struct RequestRemoveCharacter;
struct RequestRespawn;
struct PlayerRespawnRequest;
struct EnterCellRequest;

struct HostNetwork;

/**
 * @brief The session's characters on the host: hands out their session ids, knows who owns each,
 * and spawns them for players who did not see them appear.
 *
 * Players' characters keep a record of what the host's copy cannot show (appearance, base, face),
 * kept current with their movement; the host's own actors are read from its engine.
 *
 * Under the party tether everyone is where the host is, or on the way there. Arriving somewhere (a
 * door, coc, fast travel), a game loses its copies with the old cell and says where it is. Once it
 * is with the host, it gets everyone with the host and they get it; a player still on the way gets
 * nothing, so nothing lands in the wrong place.
 */
struct HostCharacters
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aAuthority.
    HostCharacters(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostCharacters);

    // The owner's connection (0: no such character).
    [[nodiscard]] ConnectionId_t OwnerOf(uint32_t aSessionId) const noexcept;
    // A session id for one of the host's actors, which its game announces under it.
    [[nodiscard]] uint32_t NewCharacterId() noexcept;
    // A joining player's character's session id (in its join answer), which only its game may announce.
    [[nodiscard]] uint32_t ReservePlayerCharacter(ConnectionId_t aPlayer) noexcept;
    // The player's own character (its session id), or none.
    [[nodiscard]] uint32_t PlayerCharacterOf(ConnectionId_t aPlayer) const noexcept;
    // Every character but aPlayer's own (aOwnSessionId), as it is now.
    void SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnSessionId) noexcept;
    // Its characters leave with it.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    // A player's own character, or (the host only) one of the host's actors, under the id the host gave it:
    // everyone else gets a spawn.
    void OnAssignCharacter(const AssignCharacterRequest& acRequest, ConnectionId_t aSender) noexcept;
    [[nodiscard]] bool IsPlayerCharacter(uint32_t aSessionId) const noexcept;
    // The owner stopped simulating the actor; its copies go where it went.
    void OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept;
    // The owner respawns its actor everywhere (a new appearance after a beast form); anyone else
    // lost its copy and asks for a fresh spawn.
    void OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept;
    // A player came back from death: everyone else respawns its character.
    void OnPlayerRespawn(const PlayerRespawnRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // Owners' movement goes straight to everyone else (unreliable: only the latest matters).
    void OnMovement(const ReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A player's game arrived somewhere: with the host, it gets everyone there and they get it.
    void OnEnterCell(const EnterCellRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Its game is where the host's is (not still on the way).
    [[nodiscard]] bool WithHost(ConnectionId_t aPlayer) const noexcept;
    // Indoors anywhere; outdoors within the host's player's loaded grid (GridCellCoords::IsCellInGridCell).
    [[nodiscard]] bool InHostGrid(const Vector3_NetQuantize& acPosition) const noexcept;
    // A player that joined where the host is not, or outdoors out of its grid, goes there (the party tether).
    void BringToParty(ConnectionId_t aPlayer) noexcept;
    // Its arrival there: the players' copies either way were made where it joined and may be gone (a copy moved
    // out of the loaded cells is), so each is sent again; a game that still has one refreshes it.
    void SendPlayersMet(ConnectionId_t aPlayer) noexcept;
    // The host's actors (not its player, whose copy travels with the party) to one player.
    void SendHostActors(ConnectionId_t aPlayer) noexcept;
    // The host's game's spawns of its actors but aExcludedSessionId, to aPlayer. How many went.
    size_t SendHostSpawns(ConnectionId_t aPlayer, uint32_t aExcludedSessionId) noexcept;
    void SendToOthersWithHost(const NotifyCharacterSpawn& acSpawn, ConnectionId_t aSender) const noexcept;

    // A spawn of the character as it is now: the host's actor from its engine, a player's from its
    // record and the host's copy.
    [[nodiscard]] std::optional<NotifyCharacterSpawn> BuildSpawn(uint32_t aSessionId) const noexcept;
    // Its copies follow aWhereabouts (where the owner's actor went).
    void RemoveCharacter(uint32_t aSessionId, const ActorWhereabouts& acWhereabouts = {}) noexcept;

    struct Character
    {
        ConnectionId_t Owner{};
        // A player's: what is needed to spawn it again, kept current with its movement.
        std::optional<NotifyCharacterSpawn> PlayerRecord;
    };

    HostAuthority& m_authority;
    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostGame& m_game;
    std::unordered_map<uint32_t, Character> m_characters;
    // Never reused in a session: a late message for a removed character finds nothing.
    uint32_t m_lastSessionId = 0;
    // Each player's character's id, from its join.
    std::unordered_map<ConnectionId_t, uint32_t> m_playerCharacters;

    // Where a player's game is: a worldspace (outdoors) or an interior cell.
    struct Place
    {
        GameId WorldSpaceId{};
        GameId CellId{};
        bool operator==(const Place&) const noexcept = default;
    };
    static Place PlaceOf(GameId aCellId, GameId aWorldSpaceId) noexcept;
    // From each player's announcement, then its arrivals.
    std::unordered_map<ConnectionId_t, Place> m_places;
    // Arrived where the host is not yet (it led, or the host is still loading): the host's actors there reach
    // them once the host arrives.
    std::unordered_set<ConnectionId_t> m_aheadOfHost;
    // Sent to the party at its join (BringToParty), not arrived yet.
    std::unordered_set<ConnectionId_t> m_broughtToParty;
    // Where the party went last (the latest player to travel by itself), for followers that arrive elsewhere.
    std::optional<NotifyPartyTravel> m_partyTravel;
};
