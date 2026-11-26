#pragma once

#include <NetImmerse/NiShadeProperty.h>

struct BSShaderProperty : NiShadeProperty
{
    virtual ~BSShaderProperty();

    // CommonLibSSE-NG BSShaderProperty (SE/AE).
    float alpha;
    int32_t lastRenderPassState;
    uint64_t flags;
    struct RenderPassArray
    {
        void* head;
        uint64_t unused;
    } renderPassList, debugRenderPassList;
    void* fadeNode;
    void* effectData;
    void* lightData;
    void* material;
    uint32_t lastAccumulatedFrameCount;
};

static_assert(offsetof(BSShaderProperty, flags) == 0x38);
static_assert(offsetof(BSShaderProperty, effectData) == 0x68);
static_assert(offsetof(BSShaderProperty, material) == 0x78);
static_assert(sizeof(BSShaderProperty) == 0x88);
