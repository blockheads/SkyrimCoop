#pragma once

#include <cstdint>

namespace KnockReplay
{
[[nodiscard]] inline uint8_t AfterAction(uint8_t aCopy, bool aGetUp, bool aTaken) noexcept
{
    return aTaken && aGetUp && aCopy >= 1 && aCopy <= 4 ? 5 : aCopy;
}
// A copy on its owner's stream: its owner's get-up action stands its own body up (BeginGetUp 40150, knock state 5);
// its graph takes the get-up from the stream.
[[nodiscard]] inline bool StandsUp(uint8_t aCopy, bool aGetUp) noexcept
{
    return AfterAction(aCopy, aGetUp, true) != aCopy;
}
// A copy on its owner's stream goes down in the step its owner's ragdoll action arrives, as the engine knocks an actor
// and tells its graph in one call (39895, ActionRagdoll); its owner's first body frame comes a moment later.
[[nodiscard]] constexpr bool GoesDown(uint8_t aCopy, bool aRagdollAction) noexcept
{
    return aRagdollAction && aCopy == 0;
}
// An actor's ragdoll bodies are its physics' in that knock state, not its animation's alone: an owner sends them and
// its living copy steers to them (docs/re/systems/physics.md, Getting up).
[[nodiscard]] constexpr bool BodiesSimulated(uint8_t aKnock) noexcept
{
    return aKnock != 0;
}
// Lying (1 to 4), not getting up (5, 6): only a lying owner's frame takes a standing copy down.
[[nodiscard]] constexpr bool Lies(uint8_t aKnock) noexcept
{
    return aKnock >= 1 && aKnock <= 4;
}
} // namespace KnockReplay
