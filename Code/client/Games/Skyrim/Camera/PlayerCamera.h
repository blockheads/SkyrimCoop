#pragma once

#include <Camera/TESCamera.h>

struct PlayerCamera : public TESCamera
{
    static PlayerCamera* Get() noexcept;

    bool IsFirstPerson() noexcept;

    bool WorldPtToScreenPt3(const NiPoint3& in, NiPoint3& out, float zeroTolerance = 1e-5f);

    void ForceFirstPerson() noexcept;
    void ForceThirdPerson() noexcept;
};
// The fields are TESCamera's: a copy of them here would shadow them at the wrong offsets.
static_assert(offsetof(PlayerCamera, state) == 0x28);
