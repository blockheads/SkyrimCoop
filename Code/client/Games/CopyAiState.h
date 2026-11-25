#pragma once

#include <cstdint>
#include <vector>

// What copies' AI state does to this game's player (docs/design/copy-contract.md, "AI state").
namespace CopyAiState
{
// Copies in combat whose combat target is this game's player (IActorBridge::SetCopiesFightingPlayer): the player
// counts as in combat from them as from the combat groups targeting it (Games/Skyrim/CopyAiState.cpp).
void SetCopiesFightingPlayer(uint32_t aCount) noexcept;
// Copies fighting the party (IActorBridge::SetCompassEnemies): the compass shows them as enemies, as it shows the
// combat groups targeting the player.
void SetCompassEnemies(std::vector<uint32_t> aFormIds) noexcept;
} // namespace CopyAiState
