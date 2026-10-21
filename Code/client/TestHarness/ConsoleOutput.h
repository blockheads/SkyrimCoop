#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// A console line's result: the console queues each line (Console::ExecuteCommand 442672) and runs the queue later
// in the frame (Console::RunQueuedCommands 442673), printing results and errors then (ConsoleLog::VPrint 51110).
namespace TestHarness::ConsoleOutput
{
// Expects that line in the queue (queue it after); its ticket. A target runs it on that reference, as the console's
// selected one (the console takes no ID.command for an id starting with a digit).
uint64_t Expect(const std::string& acLine, uint32_t aTargetFormId = 0);
// What the console printed for the ticket's line once it ran; none while it waits.
std::optional<std::vector<std::string>> Take(uint64_t aTicket);
} // namespace TestHarness::ConsoleOutput
