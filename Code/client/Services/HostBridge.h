#pragma once

#include <ENetInterface.hpp>
#include <Messages/CharacterSpawnRequest.h>
#include <Opcodes.h>

#include <array>
#include <functional>

struct ClientMessage;
struct ServerMessage;
struct ClientReferencesMoveRequest;
struct RequestInventoryChanges;
struct RequestFactionsChanges;

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
 * Most rules pass a request on as its notification, which carries the same payload
 * (Messages/Payloads.h); they differ only in who receives it (Delivery).
 */
struct HostBridge
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // Every character the host's game shows now, as spawns, but the joining player's own
    // (aJoinerServerId). The host's CharacterService::BuildSpawnRequests; a fake in the tests.
    using SpawnSource = std::function<TiltedPhoques::Vector<CharacterSpawnRequest>(uint32_t aJoinerServerId)>;

    // Installs itself on aServer (front door, late join), which calls into it from then on: the
    // bridge must outlive the server.
    HostBridge(Server::GameServer& aServer, SpawnSource aSpawns) noexcept;

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

    // Owners' movement goes straight to everyone else (unreliable: only the latest matters).
    void OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // Anyone may change an actor's items (a peer handing its follower a sword); the owner's game
    // applies it like every other copy.
    void OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Each actor's factions from its owner (one message carries many actors).
    void OnFactionsChanges(const RequestFactionsChanges& acRequest, ConnectionId_t aSender) const noexcept;

    void Deliver(const ServerMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept;

    Server::GameServer& m_server;
    SpawnSource m_spawns;
    std::array<Rule, kClientOpcodeMax> m_rules{};
};
