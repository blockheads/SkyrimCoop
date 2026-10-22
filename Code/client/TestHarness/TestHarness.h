#pragma once

#include <filesystem>

// The test harness that scripts/coop_test.py drives: a loopback command socket, control files, the
// status it polls, and the harness's own engine calls. Built only with the test_harness xmake option
// (COOP_TEST_HARNESS), never into a player's build. These three are the only calls production code
// makes into it, each behind `#if COOP_TEST_HARNESS`.
namespace TestHarness
{
// As the plugin loads: where the control files live, and SKSE's AddUITask (SKSETaskInterface; may be
// null) for console commands run where typed ones run.
void Configure(const std::filesystem::path& acPluginDirectory, void (*apAddUITask)(void*));
// Every frame from boot, on the game's main loop, before the client's update.
void Poll();
// The plugin unloads (a hot reload).
void Shutdown();
} // namespace TestHarness
