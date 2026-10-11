#pragma once

#include <ENetInterface.hpp>
#include <Messages/CharacterSpawnRequest.h>
#include <Opcodes.h>

#include <array>
#include <functional>
#include <optional>

struct ClientMessage;
struct ServerMessage;
struct ClientReferencesMoveRequest;
struct RequestInventoryChanges;
struct RequestFactionsChanges;
struct AssignCharacterRequest;
struct CancelAssignmentRequest;
struct RequestRemoveCharacter;
struct RequestRespawn;
struct PlayerRespawnRequest;

namespace Server
{
struct GameServer;
}

/**
 * @brief The host's side of the session: handles what players send, in place of the embedded
 * server's services (docs/design/host-authority-migration.md).
 *
 * It sees every message first. A message it has a rule for is handled here; anything else still
 * goes to the embedded server's services while the migration is under way. The host is a player
 * like the others: what it sends comes through here in-process, and what is forwarded to it
 * reaches its copies through the in-process route.
 *
 * The bridge hands out server ids and knows who owns each character. Most rules pass a request
 * on as its notification, which carries the same payload (Messages/Payloads.h); they differ only
 * in who receives it (Delivery).
 */
struct HostBridge
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // The host's game as it is now, for spawning characters for players who did not see them
    // appear (the host's CharacterService; a fake in the tests).
    struct HostGame
    {
        // The host's own actors, but aExcludedServerId.
        std::function<TiltedPhoques::Vector<CharacterSpawnRequest>(uint32_t aExcludedServerId)> OwnSpawns;
        // The host's copy of another player's character (position, values, items, hands, action)
        // into the spawn; false when it has no copy.
        std::function<bool(uint32_t aServerId, CharacterSpawnRequest& aSpawn)> ReadCopy;
    };

    // Installs itself on aServer (front door, leaving players), which calls into it from then on:
    // the bridge must outlive the server.
    HostBridge(Server::GameServer& aServer, HostGame aGame) noexcept;

    TP_NOCOPYMOVE(HostBridge);

    // True when the message was handled here.
    bool Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept;

private:
    enum class Delivery
    {
        FromOwner,  // the actor's owner says it; everyone else replays it
        FromAnyone, // whoever saw it happen; everyone else replays it
        ToOwner,    // something done to another's actor: its owner alone applies it
    };

    using Rule = std::function<void(const ClientMessage&, ConnectionId_t aSender)>;

    // TRequest passed on as TNotify (the same payload); apActor names the actor Delivery checks.
    template <class TRequest, class TNotify> void Relay(Delivery aDelivery, uint32_t TRequest::*apActor = nullptr) noexcept;
    // TRequest handled by a member function (rules with more to them than a relay).
    template <class TRequest> void On(void (HostBridge::*apRule)(const TRequest&, ConnectionId_t) const noexcept) noexcept;
    template <class TRequest> void On(void (HostBridge::*apRule)(const TRequest&, ConnectionId_t) noexcept) noexcept;

    // Owners' movement goes straight to everyone else (unreliable: only the latest matters).
    void OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Anyone may change an actor's items (a peer handing its follower a sword); the owner's game
    // applies it like every other copy.
    void OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Each actor's factions from its owner (one message carries many actors).
    void OnFactionsChanges(const RequestFactionsChanges& acRequest, ConnectionId_t aSender) const noexcept;

    // A player's own character, or (the host only) one of the host's actors, gets a server id; its
    // owner gets the id, everyone else a spawn. A player's first one also gets every other character.
    void OnAssignCharacter(const AssignCharacterRequest& acRequest, ConnectionId_t aSender) noexcept;
    // The owner dropped an announcement it is still waiting on, or stopped simulating the actor.
    void OnCancelAssignment(const CancelAssignmentRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept;
    // The owner respawns its actor everywhere (a new appearance after a beast form); anyone else
    // lost its copy and asks for a fresh spawn.
    void OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept;
    // A player came back from death: everyone else respawns its character.
    void OnPlayerRespawn(const PlayerRespawnRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // Its characters leave with it.
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

    // The owner's connection (0: no such character).
    [[nodiscard]] ConnectionId_t OwnerOf(uint32_t aServerId) const noexcept;
    // A spawn of the character as it is now: the host's actor from its engine, a player's from its
    // record and the host's copy.
    [[nodiscard]] std::optional<CharacterSpawnRequest> BuildSpawn(uint32_t aServerId) const noexcept;
    void SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnServerId) const noexcept;
    void RemoveCharacter(uint32_t aServerId) noexcept;

    void Deliver(const ServerMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept;

    struct Character
    {
        ConnectionId_t Owner{};
        uint32_t Cookie{}; // of the owner's announcement
        // A player's: what is needed to spawn it again (appearance, base, face), kept current with
        // its movement. The host's actors are read from its engine instead.
        std::optional<CharacterSpawnRequest> PlayerRecord;
    };

    Server::GameServer& m_server;
    HostGame m_game;
    std::array<Rule, kClientOpcodeMax> m_rules{};
    TiltedPhoques::Map<uint32_t, Character> m_characters;
};
