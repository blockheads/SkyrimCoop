#include "TestHarness/GraphStreamCheck.h"
#include "TestHarness/TestHarness.h"
#include "TestHarness/Harness.h"
#include "TestHarness/ActivateWatch.h"
#include "TestHarness/ClipLoads.h"
#include "TestHarness/Commands.h"
#include "TestHarness/EngineCalls.h"
#include "TestHarness/Status.h"
#include <core/Counters.h>
#include <Diagnostics/EngineReads.h>
#include <Main.h>

#include <future>

namespace TestHarness
{
namespace
{
std::chrono::steady_clock::time_point s_nextFilePoll{};
// The status file written off the game thread; a write still running skips the next.
std::future<void> s_statusWrite;

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
    // A frame on a destroyed window resizes a swap chain it no longer has (docs/re/systems/game-window.md).
    static bool s_windowGone = false;
    if (const HWND cWindow = EngineCalls::GameWindow(); cWindow && !s_windowGone && !IsWindow(cWindow))
    {
        s_windowGone = true;
        spdlog::error("Harness: a frame ran after the game's window was destroyed");
    }
    if (!harness.CommandsStarted)
    {
        harness.CommandsStarted = true;
        GraphStreamCheck::Install();
        if (!harness.Commands.Start(harness.Files.Directory().string(), [&harness](const std::string& acRequest) { return StartCommand(harness, acRequest); }))
            spdlog::warn("Command server did not start; the test harness falls back to control files");
    }
    harness.Inputs.Poll();
    harness.Schedule.Poll(harness);
    harness.Commands.Poll();
    harness.Events.Poll();

    // A repro's frozen world: this frame's world update ran or not, and whether the next one runs.
    if (Main* pMain = Main::Get())
    {
        const bool cRan = !pMain->freezeTime && !Diagnostics::EngineReads::GetMenuPauseCount();
        const bool cFreeze = harness.Stepper.Poll(cRan);
        if (harness.Stepper.Frozen())
            pMain->freezeTime = cFreeze;
    }

    if (harness.Files.InWorld())
    {
        // A frozen actor that died needs its update to finish dying (FrozenActors).
        harness.Frozen.Poll(EngineCalls::ActorLife, [](uint32_t aDead) {
            if (EngineCalls::FreezeActor(aDead, false))
                spdlog::info("Thawed {:X} at its death: frozen, it would never finish dying", aDead);
        }, EngineCalls::ForgetFrozen);
        harness.Actions.Poll(harness);
        harness.Input.Poll();
    }
    if (harness.Trace.Recording())
        harness.Trace.Poll();
    if (harness.Bodies.Recording() && harness.Files.InWorld())
        harness.Bodies.Poll();
    if (ActivateWatch::Watching() && harness.Files.InWorld())
        ActivateWatch::Poll();
    if (ClipLoads::Watching() && harness.Files.InWorld())
        ClipLoads::Poll();
    if (harness.Files.InWorld())
    {
        harness.Dialogue.Poll();
        harness.Gait.Poll();
        harness.Jitter.Poll();
        harness.Terrain.Poll();
    }

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
    if (s_statusWrite.valid() && s_statusWrite.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    COUNT_TIME("harness status file us");
    s_statusWrite = std::async(std::launch::async, [&files = harness.Files, status = BuildStatus(harness)] {
        try
        {
            files.WriteStatus(status.dump(2));
        }
        catch (const nlohmann::json::exception&)
        {
        }
    });
}

void Shutdown()
{
    StopQuestTrace();
    StopSampler(); // its thread runs our code
    if (s_statusWrite.valid())
        s_statusWrite.wait();
    // Off the graphs before this DLL goes (a hot reload): they would call into it.
    Harness::Get().Trace.Stop();
    Harness::Get().Inputs.Stop();
    Harness::Get().Events.Stop();
    Harness::Get().Commands.Stop();
}
} // namespace TestHarness
