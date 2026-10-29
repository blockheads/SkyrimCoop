#pragma once

#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <string_view>

namespace TestHarness
{
struct Harness;

// One command of the harness's socket: a request {"cmd": Name, ...} answered with {"ok": true, ...} or
// {"ok": false, "error": "..."}.
struct Command
{
    std::string_view Name;
    // Refused until the boot command ran (the player is not in the world before).
    bool NeedsWorld;
    nlohmann::json (*Run)(Harness& aHarness, const nlohmann::json& acRequest);
};

// By area, one file each: setting up a scenario, the player's input, reading the world, the session.
std::span<const Command> StagingCommands();
std::span<const Command> InputCommands();
std::span<const Command> WorldCommands();
std::span<const Command> KnowledgeCommands();
std::span<const Command> CrimeCommands();
std::span<const Command> SessionCommands();
std::span<const Command> MemoryCommands();
// The engine's change records (ChangeLog.h).
std::span<const Command> ChangeLogCommands();
std::span<const Command> QuestFragmentsCommands();
void StopQuestTrace() noexcept;
// Spikes kept for measuring: story events, engine probes (PlayerEvents.cpp).
std::span<const Command> PlayerEventsCommands();

nlohmann::json Ok();
nlohmann::json Error(std::string_view aMessage);

// One request line from the socket; one response line back (no newline).
std::string HandleCommand(Harness& aHarness, const std::string& acRequest);
} // namespace TestHarness
