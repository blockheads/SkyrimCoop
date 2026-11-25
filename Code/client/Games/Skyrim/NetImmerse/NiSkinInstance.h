#pragma once

#include <NetImmerse/NiAVObject.h>

#include <cstddef>

// CommonLibSSE-NG NiSkinData: what a skin's matrices are counted by (77461 reads it).
struct NiSkinData
{
    uint8_t pad0[0x58];
    uint32_t boneCount; // 58
};
static_assert(offsetof(NiSkinData, boneCount) == 0x58);

// CommonLibSSE-NG NiSkinInstance, as NiSkinInstance::UpdateBoneMatrices (77461) reads it: what is drawn follows
// boneWorldTransforms, not the nodes.
struct NiSkinInstance
{
    uint8_t pad0[0x10];
    NiSkinData* skinData;                              // 10
    void* skinPartition;                               // 18
    NiAVObject* rootParent;                            // 20
    NiAVObject** bones;                                // 28
    const NiAVObject::Transform** boneWorldTransforms; // 30
    uint32_t frameId;                                  // 38, the frame its matrices were last built
    uint32_t matrixCount;                              // 3C
    uint32_t registerCount;                            // 40
    uint32_t allocatedSize;                            // 44
    float* boneMatrices;                               // 48, 12 floats each
    float* previousBoneMatrices;                       // 50
    void* skinToWorld;                                 // 58
    CRITICAL_SECTION lock;                             // 60
};
static_assert(offsetof(NiSkinInstance, boneWorldTransforms) == 0x30);
static_assert(offsetof(NiSkinInstance, frameId) == 0x38);
static_assert(offsetof(NiSkinInstance, boneMatrices) == 0x48);
static_assert(offsetof(NiSkinInstance, lock) == 0x60);
static_assert(sizeof(NiSkinInstance) == 0x88);
