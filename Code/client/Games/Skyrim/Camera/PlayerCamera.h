#pragma once

#include <Camera/TESCamera.h>

struct Actor;

struct PlayerCamera : public TESCamera
{
    uint32_t pad38;
    uint32_t cameraTarget;
    uint8_t pad40[0x78];
    TESCameraState* cameraStates[13];
    static PlayerCamera* Get() noexcept;
    // The actor the camera follows (SetCameraTarget 50838), and its state (TESCamera::SetState 33026).
    void SetTarget(Actor* apTarget) noexcept;
    void SetMode(TESCameraState::Id aMode) noexcept;
    // A downed player's view (docs/design/death.md): another's character from behind, free look; null gives the camera
    // back to the player unless something else (a script) took it meanwhile.
    void Spectate(Actor* apWatched) noexcept;
    // Spectating is third person only: back to it when the view drops into the watched one's head (the POV key, a
    // zoom all the way in).
    void KeepSpectating() noexcept;
    // Out of the bleedout state (a dying player's death camera) into the one under it (50824).
    void LeaveBleedout(bool aInstant) noexcept;

    bool IsFirstPerson() noexcept;

    bool WorldPtToScreenPt3(const NiPoint3& in, NiPoint3& out, float zeroTolerance = 1e-5f);

    void ForceFirstPerson() noexcept;
    void ForceThirdPerson() noexcept;
};
// The fields are TESCamera's: a copy of them here would shadow them at the wrong offsets.
static_assert(offsetof(PlayerCamera, state) == 0x28);

static_assert(offsetof(PlayerCamera, cameraTarget) == 0x3C);
static_assert(offsetof(PlayerCamera, cameraStates) == 0xB8);
