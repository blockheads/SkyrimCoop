#pragma once

#include <NetImmerse/NiObject.h>

struct NiObjectNET : NiObject
{
    virtual ~NiObjectNET();

    const char* name; // 10, a BSFixedString's data (CommonLibSSE-NG)
    uint8_t unk18[0x18];
};

SKYRIM_STRUCT_ASSERT(offsetof(NiObjectNET, name) == 0x10);
