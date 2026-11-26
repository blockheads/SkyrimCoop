#pragma once

#include <Structs/PlayerOwnState.h>

struct PlayerCharacter;

// What the player's save keeps of it beside the character, read from the engine and put back on a loaded save's player
// (docs/re/systems/player-save-state.md): a guest's own over the host's save's.
namespace PlayerSaveState
{
[[nodiscard]] PlayerOwnState Read(PlayerCharacter& aPlayer) noexcept;
void Apply(PlayerCharacter& aPlayer, const PlayerOwnState& acState) noexcept;
} // namespace PlayerSaveState
