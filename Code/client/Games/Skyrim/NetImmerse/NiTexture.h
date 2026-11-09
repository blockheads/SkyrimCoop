#pragma once

#include <NetImmerse/NiObject.h>

struct NiTexture : NiObject
{
    virtual ~NiTexture();

    uint8_t formatPrefs[0x10]; // 10
    const char* name;          // 20, a BSFixedString's data (CommonLibSSE-NG)
    uint8_t unk28[0x20];
};

static_assert(offsetof(NiTexture, name) == 0x20);
static_assert(sizeof(NiTexture) == 0x48);
