#pragma once

#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>

struct HostRouter;
struct HostCharacters;
struct HostDevToolsGame;
struct HostLeash;
struct HostNetwork;
struct HostPlayers;

/**
 * @brief Any player's dev command (/spawn, /heal, ...; docs/design/dev-commands.md): the host runs a world command
 * in its game at the typer's character, then sends the line to every game, which runs a player command on its own.
 */
struct HostDevTools
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostDevTools(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
                 const HostDevToolsGame& acGame, HostLeash& aLeash) noexcept;
    NO_COPY_MOVE(HostDevTools);

  private:
    void OnDevCommand(const DevCommandRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A player's /bug: named here, so every game writes its part under the same report.
    void OnBugReport(const BugReportRequest& acRequest, ConnectionId_t aSender) const noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostDevToolsGame& m_game;
    HostLeash& m_leash;
};
