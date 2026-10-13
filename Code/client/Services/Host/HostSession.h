#pragma once

#include <ENetInterface.hpp>
#include <Structs/ServerSettings.h>

struct AuthenticationRequest;
struct HostBridge;

namespace Server
{
struct GameServer;
}

/**
 * @brief Who gets into the session: a joining game's checks, then the session's answer
 * (docs/design/join-flow.md). Player records and mod ids are still the server's until they move
 * (docs/design/host-authority-migration.md, "Server sections" 3c and 3d).
 */
struct HostSession
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    static constexpr uint32_t cMaxPlayers = 8;

    // Registers its rules with aBridge.
    HostSession(HostBridge& aBridge, Server::GameServer& aServer) noexcept;

    TP_NOCOPYMOVE(HostSession);

    // The session's settings, the same for every player.
    [[nodiscard]] static ServerSettings Settings() noexcept;

    // A game asks to join: accepted as a player, or refused and disconnected (the host's chat says why).
    void OnAuthentication(const AuthenticationRequest& acRequest, ConnectionId_t aSender) noexcept;

private:
    Server::GameServer& m_server;
};
