#pragma once

#include <Misc/IMovementState.h>

struct ActorState : IMovementState
{
    virtual ~ActorState();

    uint32_t flags1;
    uint32_t flags2;

    bool IsWeaponDrawn() const noexcept { return (flags2 >> 5 & 7) >= 3; }

    // 0 sheathed, 1 wants to draw, 2 drawing, 3 drawn, 4 wants to sheathe, 5 sheathing.
    uint8_t GetWeaponState() const noexcept { return flags2 >> 5 & 7; }
    void SetWeaponStateFlags(uint8_t aState) noexcept { flags2 = (flags2 & ~(7u << 5)) | (static_cast<uint32_t>(aState & 7) << 5); }

    // ActorState1::sneaking (CommonLibSSE-NG), what detection and Actor::IsSneaking read.
    bool IsSneaking() const noexcept { return flags1 & (1u << 9); }

    // ActorState1::swimming (CommonLibSSE-NG).
    bool IsSwimming() const noexcept { return flags1 & (1u << 10); }

    bool IsBleedingOut() const noexcept { return (flags1 & 0x1E00000) == 0x1000000 || (flags1 & 0x1E00000) == 0xE00000; }

    bool SetWeaponDrawn(bool aDraw) noexcept;
};
