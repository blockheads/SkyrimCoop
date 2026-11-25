#include <Services/DevToolsService.h>
#include <Services/TransportService.h>

#include <Bridges/IDevToolsBridge.h>
#include <Events/SessionNoticeEvent.h>
#include <Messages/Relayed.h>
#include <World.h>

DevToolsService::DevToolsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                 IDevToolsBridge& aBridge) noexcept
    : m_world(aWorld), m_transport(aTransport), m_bridge(aBridge)
{
    Listen<NotifyDevCommand, &DevToolsService::OnNotifyDevCommand>(m_connections, aDispatcher, this);
}

void DevToolsService::Run(const std::string& acLine) noexcept
{
    const auto cParsed = DevCommands::Parse(acLine);
    if (!cParsed.Run)
    {
        m_world.GetDispatcher().trigger(SessionNoticeEvent{cParsed.Error});
        return;
    }
    DevCommandRequest request;
    request.Line = acLine;
    if (m_transport.Send(request))
        return;
    // Alone: this game is the world too.
    const auto& cCommand = *cParsed.Run;
    if (cCommand.What == DevCommands::Kind::KillAll)
        m_bridge.KillHostiles();
    if (DevCommands::IsPartyHorse(cCommand))
        m_bridge.SpawnPartyHorses(0x14, cCommand.Count); // this game's player
    for (const auto& cLine : DevCommands::WorldLines(cCommand, "player"))
        m_bridge.RunConsole(cLine);
    Apply(cCommand);
}

void DevToolsService::OnNotifyDevCommand(const NotifyDevCommand& acMessage) noexcept
{
    const auto cParsed = DevCommands::Parse(acMessage.Line);
    if (!cParsed.Run)
        return;
    m_world.GetDispatcher().trigger(SessionNoticeEvent{std::format("{}: {}", acMessage.By, acMessage.Line)});
    Apply(*cParsed.Run);
}

void DevToolsService::Apply(const DevCommands::Command& acCommand) noexcept
{
    spdlog::info("DevToolsService: {}", acCommand.Text);
    if (acCommand.What == DevCommands::Kind::KillMove || acCommand.What == DevCommands::Kind::Reset)
        m_bridge.SetKillMoveOdds(acCommand.What == DevCommands::Kind::KillMove ? acCommand.Odds : std::nullopt);
    if (const auto cGod = DevCommands::GodAfter(acCommand, m_bridge.IsGodMode()))
        m_bridge.SetGodMode(*cGod);
    for (const auto& cLine : DevCommands::PlayerLines(acCommand))
        m_bridge.RunConsole(cLine);
}
