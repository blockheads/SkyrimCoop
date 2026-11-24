#pragma once

namespace ActivatePrompts
{
// TESNPC::GetActivateText (24716) over another player's character (its copy here): no prompt at all. Players don't
// talk to, search or pickpocket each other (docs/re/systems/dialogue.md, "No talking to players").
[[nodiscard]] constexpr bool Offered(bool aOtherPlayer) noexcept
{
    return !aOtherPlayer;
}
} // namespace ActivatePrompts
