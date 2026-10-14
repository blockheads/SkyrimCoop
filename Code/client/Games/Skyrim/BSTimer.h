#pragma once

// The game's clock (CommonLibSSE-NG BSTimer, singleton 410196). Its speed is set through
// SetGlobalTimeMultiplier (68245), which is hooked: the clock runs at the slower of what this game's
// engine wants and the shared multiplier (the other players' slowest).
struct BSTimer
{
    static BSTimer* Get() noexcept;
    static void SetSharedMultiplier(float aMultiplier) noexcept;
};
