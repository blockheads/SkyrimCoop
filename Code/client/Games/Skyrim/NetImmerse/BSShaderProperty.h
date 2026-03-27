#pragma once

#include <NetImmerse/NiShadeProperty.h>

struct BSShaderProperty : NiShadeProperty
{
    virtual ~BSShaderProperty();

    uint8_t pad30[0x48];

    void* material;
};

SKYRIM_STRUCT_ASSERT(offsetof(BSShaderProperty, material) == 0x78);
