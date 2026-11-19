#pragma once

#include "CommandServer.h"

#include <nlohmann/json.hpp>

#include <functional>
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
    // Instead of Run, for a large reply: reads the engine on the game thread, returns what builds the reply off it.
    std::function<nlohmann::json()> (*Gather)(Harness& aHarness, const nlohmann::json& acRequest) = nullptr;
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
// Checked actions (Actions.h).
std::span<const Command> ActionCommands();
// Whole-process stack sampling (Sampler.cpp): coop_test.py profile --stacks.
std::span<const Command> SamplerCommands();
void StopSampler() noexcept;

nlohmann::json Ok();
nlohmann::json Error(std::string_view aMessage);

// One request line from the socket, on the game thread: runs it, returns its response line (no newline), or for a
// Gather command what makes that line off the game thread (docs/re/systems/harness-cost.md).
CommandServer::Reply StartCommand(Harness& aHarness, const std::string& acRequest);
// Both at once.
std::string HandleCommand(Harness& aHarness, const std::string& acRequest);
} // namespace TestHarness
