#pragma once

#include <algorithm>

// This game's clock speed (BSTimer::SetGlobalTimeMultiplier 68245): the party's and its down camera's
// (docs/re/systems/actor-ai-and-copies.md, "A player's down camera").
struct ClockWishes
{
    static constexpr float cDownCamera = 0.8f; // BleedoutCameraState::OnEnterState's (50714)

    float Wanted = 1.f; // this game's engine asks it of every game: a kill cam, Slow Time, a bow zoom
    float Own = 1.f;    // the player's down camera: this game's alone
    float Shared = 1.f; // the other players' slowest

    // The down camera's own requests: its 0.8, and the 1 of its exit (50715) or a get-up (40150), with it current.
    [[nodiscard]] static bool FromDownCamera(float aMultiplier, bool aCameraDown) noexcept
    {
        return aCameraDown && (aMultiplier == cDownCamera || aMultiplier == 1.f);
    }

    // An engine request; true when the party is to hear Wanted. Each replaces the clock in the engine, so the down
    // camera's ends this game's party wish (a Slow Time it cut short) and another's ends the down camera's.
    [[nodiscard]] bool Ask(float aMultiplier, bool aFromDownCamera) noexcept
    {
        Own = aFromDownCamera ? aMultiplier : 1.f;
        const float cWanted = aFromDownCamera ? 1.f : aMultiplier;
        if (cWanted == Wanted)
            return false;
        Wanted = cWanted;
        return true;
    }

    [[nodiscard]] float Clock() const noexcept { return std::min({Wanted, Own, Shared}); }
};
