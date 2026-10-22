#pragma once

#include <network/ENetInterface.h>
#include <Structs/ServerSettings.h>

#include <optional>
#include <NoCopyMove.h>

struct AuthenticationRequest;
struct HostAuthority;
struct HostGame;
struct HostPlayers;

struct HostNetwork;

/**
 * @brief Who gets into the session: a joining game's checks, then the session's answer (adding it to
 * HostPlayers); the session's settings, which are the host's; and the player list every game shows,
 * as players join and leave, and the plugins' session ids (docs/design/join-flow.md).
 */
struct HostSession
{
    using ConnectionId_t = Net::ConnectionId_t;

    static constexpr uint32_t cMaxPlayers = 8;

    // Registers its rules with aAuthority.
    HostSession(HostAuthority& aAuthority, HostNetwork& aNetwork, HostPlayers& aPlayers, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostSession);

    // The session's settings, the same for every player: the host's difficulty, the co-op options'
    // defaults.
    [[nodiscard]] ServerSettings Settings() const noexcept;
    // Every frame: tells the others when the host's settings changed.
    void Update() noexcept;

    // A game asks to join: accepted as a player, or refused and disconnected (the host's chat says why).
    void OnAuthentication(const AuthenticationRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A player is leaving (still in the list): the others drop it from theirs.
    void OnPlayerLeft(ConnectionId_t aPlayer) const noexcept;

private:
    // The newcomer learns everyone already here, and they learn the newcomer.
    void Announce(ConnectionId_t aNewcomer) const noexcept;

    HostNetwork& m_network;
    HostPlayers& m_players;
    const HostGame& m_game;
    // What the others were last told (none before the first frame).
    std::optional<ServerSettings> m_sentSettings;
};
