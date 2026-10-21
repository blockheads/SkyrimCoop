#pragma once

#include <NetImmerse/NiObjectNET.h>

struct BSFixedString;

struct NiAVObject : NiObjectNET
{
    virtual ~NiAVObject();

    virtual void sub_26();
    virtual void sub_27();
    virtual void sub_28();
    virtual void sub_29();
    virtual NiAVObject* GetByName(BSFixedString& aName);

    // Rotation (row-major), position and scale, relative to the parent (local) or the world (CommonLibSSE-NG).
    struct Transform
    {
        float rotate[3][3]; // 00
        NiPoint3 translate; // 24
        float scale;        // 30
    };

    uint8_t pad30[0x48 - 0x30];
    Transform local; // 48
    Transform world; // 7C
    uint8_t padB0[0xF4 - 0xB0];
    uint32_t flags; // F4, kHidden 1 (CommonLibSSE-NG NiAVObject::Flag)
    uint8_t padF8[0x110 - 0xF8];
};

static_assert(sizeof(NiAVObject::Transform) == 0x34);
SKYRIM_STRUCT_ASSERT(offsetof(NiAVObject, local) == 0x48);
SKYRIM_STRUCT_ASSERT(offsetof(NiAVObject, world) == 0x7C);
SKYRIM_STRUCT_ASSERT(offsetof(NiAVObject, flags) == 0xF4);
SKYRIM_STRUCT_ASSERT(sizeof(NiAVObject) == 0x110);
