#pragma once

#include <filesystem>
#include <string>

namespace TestHarness
{
// The control directory, Data/SKSE/Plugins/SkyrimCoopHotReload<instance suffix>: scripts/coop_test.py
// writes boot.txt, console.txt and session.txt there and reads status.json and rpc.json (the command
// socket's port).
class ControlFiles
{
public:
    void Configure(const std::filesystem::path& acPluginDirectory);
    [[nodiscard]] const std::filesystem::path& Directory() const noexcept { return m_directory; }

    // boot.txt holds one console command (e.g. `coc QASmoke`), run once the main menu is up.
    // The engine only runs the INI's sStartingConsoleCommand on shutdown in 1.6.1170.
    void PollBootCommand();
    // console.txt: "<id> <command>"; each new id runs once (scripts/coop_test.py console).
    void PollConsoleCommand();
    // status.json, replaced atomically.
    void WriteStatus(const std::string& acStatus);

    // The boot command ran: the player is in the world and the world commands work.
    [[nodiscard]] bool InWorld() const noexcept { return m_bootState == "executed"; }
    [[nodiscard]] const std::string& BootCommand() const noexcept { return m_bootCommand; }
    [[nodiscard]] const std::string& BootState() const noexcept { return m_bootState; }
    // Id of the last console.txt request run.
    [[nodiscard]] const std::string& ConsoleId() const noexcept { return m_consoleId; }

private:
    std::filesystem::path m_directory;
    std::string m_bootCommand;
    std::string m_bootState = "none";
    std::string m_consoleId;
};
} // namespace TestHarness
