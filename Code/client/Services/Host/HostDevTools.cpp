#include <Services/Host/HostDevTools.h>

#include <Messages/Relayed.h>
#include <Services/DevCommands.h>
#include <Services/Host/HostChat.h>
#include <Services/Host/HostRouter.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostDevToolsGame.h>
#include <Services/Host/HostLeash.h>
#include <Services/Host/HostNetwork.h>
#include <Diagnostics/ReportPaths.h>
#include <Services/Host/HostPlayers.h>

HostDevTools::HostDevTools(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
                           const HostDevToolsGame& acGame, HostLeash& aLeash) noexcept
    : m_network(aNetwork), m_players(acPlayers), m_characters(acCharacters), m_game(acGame), m_leash(aLeash)
{
    aRouter.On(*this, &HostDevTools::OnDevCommand);
    aRouter.On(*this, &HostDevTools::OnBugReport);
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
    else if (cCommand.What == DevCommands::Kind::Leash || cCommand.What == DevCommands::Kind::Reset)
    {
        m_leash.SetOn(cCommand.What == DevCommands::Kind::Reset || cCommand.On.value_or(!m_leash.IsOn()));
        spdlog::info("HostDevTools: {:x} turned the leash {}", aSender, m_leash.IsOn() ? "on" : "off");
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
        if (DevCommands::IsPartyHorse(cCommand) && m_game.SpawnPartyHorses)
            spdlog::info("HostDevTools: {} party horses for {:x}", m_game.SpawnPartyHorses(cCharacter, cCommand.Count),
                         aSender);
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

void HostDevTools::OnBugReport(const BugReportRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyBugReport notify;
    notify.Text = ChatText(acRequest.Text);
    notify.Id = Diagnostics::NewReportId();
    notify.By = ChatText(m_players.PlayerNameOf(aSender));
    notify.SessionTick = m_network.GetTick();
    spdlog::info("Bug report {} from {}: {}", notify.Id, notify.By, notify.Text);
    for (const ConnectionId_t cPlayer : m_players.Connections())
        m_network.Send(cPlayer, notify);
}
