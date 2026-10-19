#pragma once

#include "TESForm.h"

struct TESQuest;

// A dialogue topic (CommonLibSSE-NG TESTopic): only what building a menu option needs.
struct TESTopic : TESForm
{
    uint8_t unk20[0x40 - 0x20]; // 20, TESFullName, data, branch
    TESQuest* ownerQuest;       // 40
};

static_assert(offsetof(TESTopic, ownerQuest) == 0x40);
