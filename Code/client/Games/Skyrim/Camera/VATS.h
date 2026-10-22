#pragma once

// The kill cam (CommonLibSSE-NG VATS, singleton 400883): VATS::SetMode (44282) sets the mode, and
// the player's controls are off while it is kKillCam.
struct VATS
{
    enum Mode : uint32_t
    {
        kNone = 0,
        kKillCam = 4
    };

    static VATS* Get() noexcept;

    uint8_t pad0[0x20];
    Mode mode;
};
static_assert(offsetof(VATS, mode) == 0x20);
