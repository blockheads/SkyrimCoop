#pragma once

#include <Games/Primitives.h>

struct NiNode;
struct NiObject;
struct NiCamera;
// One of the camera's modes (CommonLibSSE-NG TESCameraState).
struct TESCameraState
{
    // CommonLibSSE-NG CameraState.
    enum Id : uint32_t
    {
        kFirstPerson = 0,
        kAutoVanity,
        kVATS,
        kFree,
        kIronSights,
        kFurniture,
        kPCTransition,
        kTween,
        kAnimated,
        kThirdPerson,
        kMount,
        kBleedout,
        kDragon
    };

    void* vtable;        // 00
    uint32_t refCount;   // 08
    uint32_t pad0C;      // 0C
    void* camera;        // 10
    Id id;               // 18
    uint32_t pad1C;      // 1C
};
static_assert(offsetof(TESCameraState, id) == 0x18);

struct TESCamera
{
    virtual ~TESCamera(){};

    virtual void SetNode(NiNode* node){};
    virtual void Update(){};

    NiCamera* GetNiCamera();

    float rotZ;
    float rotX;
    NiPoint3 pos;
    float zoom;
    NiNode* cameraNode;     // 20, CommonLib cameraRoot
    TESCameraState* state;  // 28, CommonLib currentState
    bool unk;
};
SKYRIM_STRUCT_ASSERT(offsetof(TESCamera, cameraNode) == 0x20);
SKYRIM_STRUCT_ASSERT(offsetof(TESCamera, state) == 0x28);
