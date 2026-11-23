#pragma once

#include <Forms/TESBoundObject.h>

struct TESObjectWEAP : TESBoundObject
{
    uint8_t unk30[0xE8 - sizeof(TESBoundObject)];
    // BGSEquipType (+0xE0, TESBoundObject::GetEquipType 14218)'s slot (+0x08, 14216): BothHands for a bow.
    TESForm* equipSlot;
};
static_assert(offsetof(TESObjectWEAP, equipSlot) == 0xE8);
