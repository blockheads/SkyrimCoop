#pragma once

namespace ContainerOpens
{
// ContainerMenu::OpenMenu (51140) refused on another player's character (its copy here, ActorExtension's flags):
// players don't loot or pickpocket each other (docs/design/party-items.md).
[[nodiscard]] constexpr bool Refused(bool aOtherPlayer) noexcept
{
    return aOtherPlayer;
}
} // namespace ContainerOpens
