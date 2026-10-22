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

    struct NiNode* parent;                      // 30
    uint32_t parentIndex;                       // 38
    uint32_t unk3C;
    struct bhkNiCollisionObject* collisionObject; // 40, its physics (NiPointer)
    Transform local; // 48
    Transform world; // 7C
    uint8_t padB0[0xF4 - 0xB0];
    uint32_t flags; // F4, kHidden 1 (CommonLibSSE-NG NiAVObject::Flag)
    uint8_t padF8[0x110 - 0xF8];
};

static_assert(sizeof(NiAVObject::Transform) == 0x34);
static_assert(offsetof(NiAVObject, collisionObject) == 0x40);
static_assert(offsetof(NiAVObject, local) == 0x48);
static_assert(offsetof(NiAVObject, world) == 0x7C);
static_assert(offsetof(NiAVObject, flags) == 0xF4);
static_assert(sizeof(NiAVObject) == 0x110);
