#include <Services/Host/HostDevTools.h>

#include <Messages/Relayed.h>
#include <Services/DevCommands.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

HostDevTools::HostDevTools(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
                           const HostGame& acGame) noexcept
    : m_players(acPlayers), m_characters(acCharacters), m_game(acGame)
{
    aAuthority.On(*this, &HostDevTools::OnDevCommand);
}

void HostDevTools::OnDevCommand(const DevCommandRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    const auto cParsed = DevCommands::Parse(acRequest.Line);
    if (!cParsed.Run)
    {
        spdlog::warn("HostDevTools: {:x} sent a line that is no dev command: {}", aSender, acRequest.Line);
        return;
    }
    const DevCommands::Command& cCommand = *cParsed.Run;
    if (cCommand.What == DevCommands::Kind::KillAll)
    {
        const uint32_t cKilled = m_game.KillHostiles ? m_game.KillHostiles() : 0;
        spdlog::info("HostDevTools: /killall from {:x} killed {} actors", aSender, cKilled);
    }
    else if (DevCommands::IsWorld(cCommand.What))
    {
        // Placed at the typer's character: the host's game shows it (its player, or that player's copy).
        const uint32_t cCharacter = m_characters.PlayerCharacterOf(aSender);
        const auto cAt = cCharacter && m_game.ConsoleNameOf ? m_game.ConsoleNameOf(cCharacter) : std::nullopt;
        if (!cAt && cCommand.What == DevCommands::Kind::Spawn)
        {
            spdlog::warn("HostDevTools: {:x}'s character is not loaded in the host's game: {} not run", aSender,
                         acRequest.Line);
            return;
        }
        for (const auto& cLine : DevCommands::WorldLines(cCommand, cAt.value_or("player")))
            if (m_game.RunConsole)
                m_game.RunConsole(cLine);
    }
    // Every game, the typer's too: the chat names who ran it, and a player command runs on each player.
    NotifyDevCommand notify;
    notify.Line = cCommand.Text;
    notify.By = ChatText(m_players.PlayerNameOf(aSender));
    m_players.SendToAll(notify);
}
