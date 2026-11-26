#pragma once

#include <Camera/TESCamera.h>

struct ThirdPersonState : TESCameraState
{
    uint8_t pad20[0xB4];
    float freeYaw;
    float freePitch;
    bool freeRotationEnabled;
    uint8_t padDD[0xB];
};
static_assert(offsetof(ThirdPersonState, freeYaw) == 0xD4);
static_assert(offsetof(ThirdPersonState, freeRotationEnabled) == 0xDC);
static_assert(sizeof(ThirdPersonState) == 0xE8);
