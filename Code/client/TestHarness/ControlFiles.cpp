#include "ControlFiles.h"
#include "EngineCalls.h"

#include <PluginPaths.h>
#include <World.h>
#include <Bridges/IPlayerBridge.h>

#include <fstream>

namespace TestHarness
{
void ControlFiles::Configure(const std::filesystem::path& acPluginDirectory)
{
    m_directory = acPluginDirectory / ("SkyrimCoopHotReload" + PluginPaths::InstanceSuffix());
}

void ControlFiles::PollBootCommand()
{
    if (m_bootState == "none")
    {
        std::ifstream bootFile(m_directory / "boot.txt");
        if (!bootFile || !std::getline(bootFile, m_bootCommand) || m_bootCommand.empty())
        {
            // Booted to the main menu: in the world once a save loads (a join from there).
            if (entt::locator<World>::has_value() && World::Get().GetPlayerBridge().IsPlayerLoaded() &&
                World::Get().GetPlayerBridge().GetPlayerParentCellId() != 0 && !EngineCalls::IsMenuOpen("Main Menu"))
                m_bootState = "executed";
            return;
        }
        m_bootState = "waiting_for_main_menu";
    }
    if (m_bootState != "waiting_for_main_menu")
        return;

    // Hot reloaded into a running game: the boot command ran in an earlier load.
    if (entt::locator<World>::has_value())
    {
        auto& player = World::Get().GetPlayerBridge();
        if (player.IsPlayerLoaded() && player.GetPlayerParentCellId() != 0)
        {
            spdlog::info("Boot command skipped: already in the world");
            m_bootState = "executed";
            return;
        }
    }

    if (!EngineCalls::IsMenuOpen("Main Menu"))
        return;

    spdlog::info("Boot command: {}", m_bootCommand);
    EngineCalls::ExecuteConsoleCommand(m_bootCommand.c_str());
    m_bootState = "executed";
}

void ControlFiles::PollConsoleCommand()
{
    std::ifstream consoleFile(m_directory / "console.txt");
    std::string line;
    if (!consoleFile || !std::getline(consoleFile, line))
        return;
    const auto cSeparator = line.find(' ');
    if (cSeparator == std::string::npos || line.compare(0, cSeparator, m_consoleId) == 0)
        return;
    m_consoleId = line.substr(0, cSeparator);
    spdlog::info("Console command {}: {}", m_consoleId, line.substr(cSeparator + 1));
    EngineCalls::RunConsoleCommand(line.substr(cSeparator + 1));
}

void ControlFiles::WriteStatus(const std::string& acStatus)
{
    const auto cTemporary = m_directory / "status.json.tmp";
    const auto cDestination = m_directory / "status.json";
    std::ofstream output(cTemporary, std::ios::trunc);
    output << acStatus << '\n';
    output.close();
    if (output)
        MoveFileExW(cTemporary.c_str(), cDestination.c_str(), MOVEFILE_REPLACE_EXISTING);
}
} // namespace TestHarness
