#pragma once

// The console (docs/re/systems/console.md).
namespace ConsoleLines
{
// Queues the line as typed; it runs later in the frame (Console::ExecuteCommand 442672, RunQueuedCommands 442673).
void Execute(const char* acpLine) noexcept;
} // namespace ConsoleLines
