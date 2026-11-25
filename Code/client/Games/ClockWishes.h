#pragma once

#include <algorithm>

// This game's clock speed (BSTimer::SetGlobalTimeMultiplier 68245): the party's and its down camera's
// (docs/re/systems/actor-ai-and-copies.md, "A player's down camera").
struct ClockWishes
{
    float Wanted = 1.f; // this game's engine asks it of every game: a kill cam, Slow Time, a bow zoom
    float Own = 1.f;    // the player's down camera (BleedoutCameraState 50714): this game's alone
    float Shared = 1.f; // the other players' slowest

    // An engine request, made with the player's camera down or not; true when the party is to hear of it.
    [[nodiscard]] bool Ask(float aMultiplier, bool aDownCamera) noexcept
    {
        Own = aDownCamera ? aMultiplier : 1.f;
        // Leaving the down camera puts the whole clock back to 1, as vanilla's does.
        if ((aDownCamera && aMultiplier < 1.f) || aMultiplier == Wanted)
            return false;
        Wanted = aMultiplier;
        return true;
    }

    [[nodiscard]] float Clock() const noexcept { return std::min({Wanted, Own, Shared}); }
};
