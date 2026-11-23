#pragma once

#include <Forms/TESBoundObject.h>

struct TESObjectWEAP : TESBoundObject
{
    // WEAPON_DATA::animationType (CommonLibSSE weaponData +0x190, +0x0D): 5-7 and 9 are held in both hands
    // (two-handed sword and axe, bow, crossbow), as ActorProcess 39811 tests it.
    [[nodiscard]] bool IsTwoHanded() const noexcept { return (animationType >= 5 && animationType <= 7) || animationType == 9; }

    uint8_t unk30[0x19D - sizeof(TESBoundObject)];
    uint8_t animationType;
};
static_assert(offsetof(TESObjectWEAP, animationType) == 0x19D);
