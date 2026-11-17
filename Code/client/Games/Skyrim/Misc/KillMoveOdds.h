#pragma once

#include <cstdint>
#include <optional>

// /killmove (docs/design/dev-commands.md): the odds the kill move conditions answer with, this game's attackers'.
namespace KillMoveOdds
{
void Set(std::optional<uint8_t> aOdds) noexcept;
} // namespace KillMoveOdds
