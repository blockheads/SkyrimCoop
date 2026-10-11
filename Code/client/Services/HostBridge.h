#pragma once

#include <ENetInterface.hpp>

struct ClientMessage;
struct ClientReferencesMoveRequest;

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

    explicit HostBridge(Server::GameServer& aServer) noexcept;

    TP_NOCOPYMOVE(HostBridge);

    // True when the message was handled here.
    bool Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept;

private:
    // Owners' movement goes straight to everyone else (unreliable: only the latest matters).
    void OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) const noexcept;

    Server::GameServer& m_server;
};
