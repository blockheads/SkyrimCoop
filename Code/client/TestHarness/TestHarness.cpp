#include <ClientPch.h>
#include "TestHarness.h"
#include "Harness.h"
#include "Commands.h"
#include "EngineCalls.h"
#include "Status.h"

namespace TestHarness
{
namespace
{
std::chrono::steady_clock::time_point s_nextFilePoll{};

// Tutorial help popups ("Magicka Low", "Health Low"...) swallow all input and console commands
// until closed, which stalls the harness: close them as soon as they open.
void CloseTutorialPopups()
{
    if (!EngineCalls::IsMenuOpen("Tutorial Menu"))
        return;
    EngineCalls::SetMenuShown("Tutorial Menu", false);
    spdlog::info("Closed a tutorial popup");
}
} // namespace

Harness& Harness::Get()
{
    // Process-lifetime. Shut down from the hot reload, never from CRT destructors/DllMain.
    static Harness* s_harness = new Harness;
    return *s_harness;
}

void Configure(const std::filesystem::path& acPluginDirectory, void (*apAddUITask)(void*))
{
    Harness::Get().Files.Configure(acPluginDirectory);
    EngineCalls::SetUITaskQueue(apAddUITask);
}

void Poll()
{
    auto& harness = Harness::Get();
    if (harness.Files.Directory().empty())
        return;
    EngineCalls::EndConsolePauses();
    if (!harness.CommandsStarted)
    {
        harness.CommandsStarted = true;
        if (!harness.Commands.Start(harness.Files.Directory().string(), [&harness](const std::string& acRequest) { return HandleCommand(harness, acRequest); }))
            spdlog::warn("Command server did not start; the test harness falls back to control files");
    }
    harness.Logs.Poll();
    harness.Inputs.Poll();
    harness.Commands.Poll();
    harness.Events.Poll();

    if (harness.Files.InWorld())
        harness.Input.Poll();
    if (harness.Trace.Recording())
        harness.Trace.Poll();

    const auto cNow = std::chrono::steady_clock::now();
    if (cNow < s_nextFilePoll)
        return;
    s_nextFilePoll = cNow + std::chrono::milliseconds(250);
    harness.Files.PollBootCommand();
    if (harness.Files.InWorld())
    {
        harness.Files.PollConsoleCommand();
        CloseTutorialPopups();
    }
    harness.Files.WriteStatus(BuildStatus(harness).dump(2));
}

void Shutdown()
{
    // Off the graphs before this DLL goes (a hot reload): they would call into it.
    Harness::Get().Trace.Stop();
    Harness::Get().Inputs.Stop();
    Harness::Get().Events.Stop();
    Harness::Get().Commands.Stop();
}
} // namespace TestHarness
