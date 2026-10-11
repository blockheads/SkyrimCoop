#pragma once

#include <ENetInterface.hpp>
#include <Messages/CharacterSpawnRequest.h>

#include <functional>


struct ClientMessage;
struct ServerMessage;
struct ClientReferencesMoveRequest;
struct RequestInventoryChanges;
struct RequestEquipmentChanges;
struct RequestEquipmentSnapshot;
struct DrawWeaponRequest;
struct RequestActorValueChanges;
struct RequestActorMaxValueChanges;
struct RequestHealthChangeBroadcast;
struct RequestDeathStateChange;

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
 */
struct HostBridge
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // Every character the host's game shows now, as spawns, but the joining player's own
    // (aJoinerServerId). The host's CharacterService::BuildSpawnRequests; a fake in the tests.
    using SpawnSource = std::function<TiltedPhoques::Vector<CharacterSpawnRequest>(uint32_t aJoinerServerId)>;

    // Installs itself on aServer (front door, late join), which calls into it
    // from then on: the bridge must outlive the server.
    HostBridge(Server::GameServer& aServer, SpawnSource aSpawns) noexcept;

    TP_NOCOPYMOVE(HostBridge);

    // True when the message was handled here.
    bool Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept;

private:
    // Owners' movement goes straight to everyone else (unreliable: only the latest matters).
    void OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // Anyone may change an actor's items (a peer handing its follower a sword); the owner's game
    // applies it like every other copy.
    void OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    void OnEquipmentChanges(const RequestEquipmentChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Only the owner says what its actor wears and whether its hands are up.
    void OnEquipmentSnapshot(const RequestEquipmentSnapshot& acRequest, ConnectionId_t aSender) const noexcept;
    void OnDrawWeapon(const DrawWeaponRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // An actor's values and death come from its owner.
    void OnActorValues(const RequestActorValueChanges& acRequest, ConnectionId_t aSender) const noexcept;
    void OnActorMaxValues(const RequestActorMaxValueChanges& acRequest, ConnectionId_t aSender) const noexcept;
    void OnDeathState(const RequestDeathStateChange& acRequest, ConnectionId_t aSender) const noexcept;
    // A hit on someone else's actor goes to its owner alone, which applies it; everyone else then
    // gets the owner's resulting health as values.
    void OnHealthChange(const RequestHealthChangeBroadcast& acRequest, ConnectionId_t aSender) const noexcept;

    // Sends acNotify to everyone but aSender when aSender owns aServerId.
    void RelayFromOwner(uint32_t aServerId, const ServerMessage& acNotify, ConnectionId_t aSender) const noexcept;

    Server::GameServer& m_server;
    SpawnSource m_spawns;
};
