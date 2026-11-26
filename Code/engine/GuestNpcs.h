#pragma once

#include <cstdint>

// A guest's game holds the NPCs its own engine runs until the host spawns their copies: no AI (HookUpdateAI), no 3D
// (HookLoad3D). docs/design/copy-contract.md, "A guest's own NPCs".
namespace GuestNpcs
{
// On while a guest (GuestEngine::SetDecidesWorld(false)); each start is a new session.
void SetHolding(bool aOn) noexcept;
// This guest session (0: not a guest). From any thread: Load3D runs on the loader threads.
[[nodiscard]] uint32_t Session() noexcept;

// Held: while a guest, any actor but this game's player, a copy, or one this game claimed in this session (its
// ridden mount: SetActorCopy(false) stamps aClaimedIn).
[[nodiscard]] constexpr bool Held(uint32_t aSession, bool aThisPlayer, bool aCopy, uint32_t aClaimedIn) noexcept
{
    return aSession != 0 && !aThisPlayer && !aCopy && aClaimedIn != aSession;
}
} // namespace GuestNpcs
