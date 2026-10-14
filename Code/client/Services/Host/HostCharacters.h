#pragma once

#include <ENetInterface.hpp>
#include <Messages/CharacterSpawnRequest.h>

#include <optional>

struct HostBridge;
struct HostGame;
struct HostPlayers;
struct ClientReferencesMoveRequest;
struct AssignCharacterRequest;
struct CancelAssignmentRequest;
struct RequestRemoveCharacter;
struct RequestRespawn;
struct PlayerRespawnRequest;
struct EnterCellRequest;

namespace Server
{
struct GameServer;
}

/**
 * @brief The session's characters on the host: hands out their server ids, knows who owns each,
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
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // Registers its rules with aBridge.
    HostCharacters(HostBridge& aBridge, Server::GameServer& aServer, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    TP_NOCOPYMOVE(HostCharacters);

    // The owner's connection (0: no such character).
    [[nodiscard]] ConnectionId_t OwnerOf(uint32_t aServerId) const noexcept;
    // Every character but aPlayer's own (aOwnServerId), as it is now.
    void SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnServerId) const noexcept;
    // Its characters leave with it.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    // A player's own character, or (the host only) one of the host's actors, gets a server id; its
    // owner gets the id, everyone else a spawn.
    void OnAssignCharacter(const AssignCharacterRequest& acRequest, ConnectionId_t aSender) noexcept;
    // The owner dropped an announcement it is still waiting on.
    void OnCancelAssignment(const CancelAssignmentRequest& acRequest, ConnectionId_t aSender) noexcept;
    // The owner stopped simulating the actor.
    void OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept;
    // The owner respawns its actor everywhere (a new appearance after a beast form); anyone else
    // lost its copy and asks for a fresh spawn.
    void OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept;
    // A player came back from death: everyone else respawns its character.
    void OnPlayerRespawn(const PlayerRespawnRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // Owners' movement goes straight to everyone else (unreliable: only the latest matters).
    void OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A player's game arrived somewhere: with the host, it gets everyone there and they get it.
    void OnEnterCell(const EnterCellRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Its game is where the host's is (not still on the way).
    [[nodiscard]] bool WithHost(ConnectionId_t aPlayer) const noexcept;
    void SendToOthersWithHost(const CharacterSpawnRequest& acSpawn, ConnectionId_t aSender) const noexcept;

    // A spawn of the character as it is now: the host's actor from its engine, a player's from its
    // record and the host's copy.
    [[nodiscard]] std::optional<CharacterSpawnRequest> BuildSpawn(uint32_t aServerId) const noexcept;
    void RemoveCharacter(uint32_t aServerId) noexcept;

    struct Character
    {
        ConnectionId_t Owner{};
        uint32_t Cookie{}; // of the owner's announcement
        // A player's: what is needed to spawn it again, kept current with its movement.
        std::optional<CharacterSpawnRequest> PlayerRecord;
    };

    HostBridge& m_bridge;
    Server::GameServer& m_server;
    const HostPlayers& m_players;
    const HostGame& m_game;
    TiltedPhoques::Map<uint32_t, Character> m_characters;

    // Where a player's game is: a worldspace (outdoors) or an interior cell.
    struct Place
    {
        GameId WorldSpaceId{};
        GameId CellId{};
        bool operator==(const Place&) const noexcept = default;
    };
    static Place PlaceOf(GameId aCellId, GameId aWorldSpaceId) noexcept;
    // From each player's announcement, then its arrivals.
    TiltedPhoques::Map<ConnectionId_t, Place> m_places;
};
