#pragma once

struct CombatInventory
{
    uint8_t unk0[0x19C];
    float closeRange;    // 19C: the out-of-range check's base radius (close movement)
    float reach;         // 1A0: this fighter's own reach, compared with the target's
    uint8_t unk1A4[0x1B8 - 0x1A4];
    float maximumRange;
    uint8_t unk1BC[0x1C8 - 0x1BC];
};
static_assert(offsetof(CombatInventory, closeRange) == 0x19C);
static_assert(offsetof(CombatInventory, reach) == 0x1A0);
static_assert(sizeof(CombatInventory) == 0x1C8);
