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

    // ActorState1::sitSleepState (CommonLibSSE-NG SIT_SLEEP_STATE): 3 sitting, 7 sleeping, 1-2/4-6/8 on the way.
    uint8_t GetSitSleepState() const noexcept { return flags1 >> 14 & 0xF; }

    // ActorState1::lifeState (CommonLibSSE-NG ACTOR_LIFE_STATE): 0 alive, 1 dying, 2 dead, ...
    static constexpr uint8_t kLifeDying = 1;
    static constexpr uint8_t kLifeRestrained = 6;
    static uint8_t LifeStateOf(uint32_t aFlags1) noexcept { return aFlags1 >> 21 & 0xF; }

    bool IsBleedingOut() const noexcept { return (flags1 & 0x1E00000) == 0x1000000 || (flags1 & 0x1E00000) == 0xE00000; }

    bool SetWeaponDrawn(bool aDraw) noexcept;
};
