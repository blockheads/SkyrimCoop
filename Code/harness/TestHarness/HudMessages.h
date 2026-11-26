#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The HUD's notifications this game showed (Debug.Notification, Message.Show's), the latest last: a scenario reads
// whether another game's script's message reached this one (docs/design/party-calls.md).
namespace TestHarness::HudMessages
{
// aShown: how many lines this game has shown in all, a cursor for what came after.
[[nodiscard]] std::vector<std::string> Recent(uint64_t& aShown);
} // namespace TestHarness::HudMessages
