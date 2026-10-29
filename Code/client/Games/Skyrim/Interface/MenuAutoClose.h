#pragma once

#include <algorithm>
#include <string_view>

// When a live menu on something closes because the world changed under it (a container's, a merchant's barter):
// Skyrim Souls RE's AutoCloseManager, with its defaults. No engine: ClientTests.
namespace MenuAutoClose
{
constexpr float kDistance = 400.f;  // units: further than this from its target, the menu closes
constexpr float kTolerance = 100.f; // opened further than kDistance: closes once it gets this much further still

struct Target
{
    bool Gone{};     // no reference any more, or disabled or deleted
    bool Actor{};
    bool Dead{};
    float Distance{}; // from this game's player
};

struct Watch
{
    Target AtOpen{};
    float Closest{};
};

inline Watch Open(const Target& acTarget) noexcept
{
    return {acTarget, acTarget.Distance};
}

// Why the menu closes now, empty to keep it open.
inline std::string_view Check(Watch& aWatch, const Target& acNow) noexcept
{
    if (acNow.Gone && !aWatch.AtOpen.Gone)
        return "its target is gone";
    if (acNow.Actor && acNow.Dead && !aWatch.AtOpen.Dead)
        return "its target died";
    const bool cOpenedFar = aWatch.AtOpen.Distance > kDistance;
    if (!cOpenedFar && acNow.Distance > kDistance)
        return "its target moved out of range";
    if (cOpenedFar && acNow.Distance > kDistance && acNow.Distance > aWatch.Closest + kTolerance)
        return "its target moved further out of range";
    aWatch.Closest = std::min(aWatch.Closest, acNow.Distance);
    return {};
}
} // namespace MenuAutoClose
