#pragma once

#include <cstdint>

// What copies' AI state does to this game's player (docs/design/copy-contract.md, "AI state").
namespace CopyAiState
{
// Copies in combat whose combat target is this game's player (IActorBridge::SetCopiesFightingPlayer): the player
// counts as in combat from them as from the combat groups targeting it (Games/Skyrim/CopyAiState.cpp).
void SetCopiesFightingPlayer(uint32_t aCount) noexcept;
} // namespace CopyAiState
