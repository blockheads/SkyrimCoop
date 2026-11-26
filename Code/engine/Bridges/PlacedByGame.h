#pragma once

#include <cstdint>

// References this game itself just placed (a script's or the console's move, seen by the host's change funnel as it
// happens), until the host has read where: nothing steers one meanwhile, or the place read would be the steered one.
// Any thread; a leaf lock.
namespace PlacedByGame
{
void Mark(uint32_t aFormId) noexcept;
[[nodiscard]] bool Is(uint32_t aFormId) noexcept;
void Clear(uint32_t aFormId) noexcept;
void ClearAll() noexcept;
} // namespace PlacedByGame
