#pragma once

#include <NetImmerse/NiAVObject.h>
#include <NetImmerse/NiPointer.h>

struct BSShaderProperty;

// CommonLibSSE-NG BSGeometry, SE/AE layout.
struct BSGeometry : NiAVObject
{
    uint8_t modelBound[0x10];
    void* alphaProperty;
    NiPointer<BSShaderProperty> shaderProperty;
    void* skinInstance;
    void* rendererData;
    void* unk140;
    uint64_t vertexDesc;
    uint8_t type;
    uint8_t pad151[7];
};

static_assert(offsetof(BSGeometry, shaderProperty) == 0x128);
static_assert(offsetof(BSGeometry, vertexDesc) == 0x148);
static_assert(sizeof(BSGeometry) == 0x158);
