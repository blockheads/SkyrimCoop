#pragma once

#include <ENetInterface.hpp>
#include <Structs/ServerSettings.h>

#include <optional>

struct AuthenticationRequest;
struct HostBridge;
struct HostGame;

namespace Server
{
struct GameServer;
}

/**
 * @brief Who gets into the session: a joining game's checks, then the session's answer, and the
 * session's settings, which are the host's (docs/design/join-flow.md). Player records and mod ids are still the server's until they move
 * (docs/design/host-authority-migration.md, "Server sections" 3c and 3d).
 */
struct HostSession
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    static constexpr uint32_t cMaxPlayers = 8;

    // Registers its rules with aBridge.
    HostSession(HostBridge& aBridge, Server::GameServer& aServer, const HostGame& acGame) noexcept;

    TP_NOCOPYMOVE(HostSession);

    // The session's settings, the same for every player: the host's difficulty, the co-op options'
    // defaults.
    [[nodiscard]] ServerSettings Settings() const noexcept;
    // Every frame: tells the others when the host's settings changed.
    void Update() noexcept;

    // A game asks to join: accepted as a player, or refused and disconnected (the host's chat says why).
    void OnAuthentication(const AuthenticationRequest& acRequest, ConnectionId_t aSender) noexcept;

private:
    Server::GameServer& m_server;
    const HostGame& m_game;
    // What the others were last told (none before the first frame).
    std::optional<ServerSettings> m_sentSettings;
};
