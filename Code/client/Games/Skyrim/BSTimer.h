#pragma once

#include <cstddef>
#include <cstdint>

// The game's clock (CommonLibSSE-NG BSTimer, singleton 410196). Its speed is set through
// SetGlobalTimeMultiplier (68245), which is hooked: the clock runs at the slower of what this game's
// engine wants and the shared multiplier (the other players' slowest).
struct BSTimer
{
    static BSTimer* Get() noexcept;
    static void SetSharedMultiplier(float aMultiplier) noexcept;

    uint8_t unk0[0x18];
    float Delta;         // 18: the last frame in game time (RealTimeDelta times the multiplier)
    float RealTimeDelta; // 1C: the last frame, seconds, measured at the frame's start (68244)
    uint8_t unk20[0x20];
};
static_assert(offsetof(BSTimer, RealTimeDelta) == 0x1C && sizeof(BSTimer) == 0x40);
