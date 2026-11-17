#pragma once

#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostPlayers;

/**
 * @brief Any player's dev command (/spawn, /heal, ...; docs/design/dev-commands.md): the host runs a world command
 * in its game at the typer's character, then sends the line to every game, which runs a player command on its own.
 */
struct HostDevTools
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostDevTools(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
                 const HostGame& acGame) noexcept;
    NO_COPY_MOVE(HostDevTools);

  private:
    void OnDevCommand(const DevCommandRequest& acRequest, ConnectionId_t aSender) const noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
