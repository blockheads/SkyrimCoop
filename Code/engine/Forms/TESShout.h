#pragma once

#include <Forms/TESForm.h>

#include <cstddef>

struct SpellItem;

// A shout (CommonLibSSE-NG TESShout): its three words of power, each with its spell.
struct TESShout : TESForm
{
    struct Variation
    {
        TESForm* word; // a TESWordOfPower
        SpellItem* spell;
        float recoveryTime;
        uint32_t pad14;
    };

    uint8_t components[0x40]; // 20: TESFullName, BGSMenuDisplayObject, BGSEquipType, TESDescription
    Variation variations[3];  // 60: SNAM
};

static_assert(sizeof(TESShout::Variation) == 0x18);
static_assert(offsetof(TESShout, variations) == 0x60);
static_assert(sizeof(TESShout) == 0xA8);
