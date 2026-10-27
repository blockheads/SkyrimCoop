#pragma once

#include <string>
#include <vector>

// The HUD's notifications this game showed (Debug.Notification, Message.Show's), the latest last: a scenario reads
// whether another game's script's message reached this one (docs/design/party-calls.md).
namespace TestHarness::HudMessages
{
[[nodiscard]] std::vector<std::string> Recent();
} // namespace TestHarness::HudMessages
