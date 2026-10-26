
#include <Camera/PlayerCamera.h>
#include <Camera/ThirdPersonState.h>
#include <PlayerCharacter.h>
#include <NetImmerse/NiCamera.h>
#include <VersionDb.h>
PlayerCamera* PlayerCamera::Get() noexcept
{
    POINTER_SKYRIMSE(PlayerCamera*, s_instance, 400802);
    return *(s_instance.Get());
}

bool PlayerCamera::IsFirstPerson() noexcept
{
    using TIsFirstPerson = void(PlayerCamera*, void*, void*, double*);
    POINTER_SKYRIMSE(TIsFirstPerson, isFirstPerson, 21600);

    double firstPerson = 0.0;
    isFirstPerson(this, nullptr, nullptr, &firstPerson);

    return firstPerson == 1.0;
}

bool PlayerCamera::WorldPtToScreenPt3(const NiPoint3& in, NiPoint3& out, float zeroTolerance /* = 1e-5f */)
{
    auto* pCam = GetNiCamera();
    if (cameraNode && pCam)
    {
        return pCam->WorldPtToScreenPt3(in, out, zeroTolerance);
    }

    return false;
}

void PlayerCamera::ForceFirstPerson() noexcept
{
    using TForceFirstPerson = void(PlayerCamera*);
    POINTER_SKYRIMSE(TForceFirstPerson, forceFirstPerson, 50790);
    forceFirstPerson(this);
}

void PlayerCamera::ForceThirdPerson() noexcept
{
    using TForceThirdPerson = void(PlayerCamera*);
    POINTER_SKYRIMSE(TForceThirdPerson, forceThirdPerson, 50796);
    forceThirdPerson(this);
}

void PlayerCamera::SetTarget(Actor* apTarget) noexcept
{
    using TSetTarget = void(PlayerCamera*, Actor*);
    POINTER_SKYRIMSE(TSetTarget, s_setTarget, 50838);
    s_setTarget(this, apTarget);
}

void PlayerCamera::SetMode(TESCameraState::Id aMode) noexcept
{
    using TSetState = void(TESCamera*, TESCameraState*);
    POINTER_SKYRIMSE(TSetState, s_setState, 33026);
    if (cameraStates[aMode])
        s_setState(this, cameraStates[aMode]);
}

namespace
{
// The spectator view (one camera per process): the target it set, and the player's free look before it.
bool s_spectating = false;
bool s_freeRotationBefore = false;
uint32_t s_watched = 0;
} // namespace

void PlayerCamera::Spectate(Actor* apWatched) noexcept
{
    auto* pThirdPerson = static_cast<ThirdPersonState*>(cameraStates[TESCameraState::kThirdPerson]);
    if (!apWatched)
    {
        if (s_spectating && cameraTarget == s_watched)
        {
            if (pThirdPerson)
                pThirdPerson->freeRotationEnabled = s_freeRotationBefore;
            SetTarget(PlayerCharacter::Get());
            SetMode(TESCameraState::kThirdPerson);
        }
        s_spectating = false;
        return;
    }
    if (!s_spectating && pThirdPerson)
        s_freeRotationBefore = pThirdPerson->freeRotationEnabled;
    SetTarget(apWatched);
    SetMode(TESCameraState::kThirdPerson);
    if (pThirdPerson)
    {
        pThirdPerson->freeRotationEnabled = true;
        pThirdPerson->freeYaw = pThirdPerson->freePitch = 0.f;
    }
    s_watched = cameraTarget;
    s_spectating = true;
}

void PlayerCamera::LeaveBleedout(bool aInstant) noexcept
{
    using TLeaveBleedout = void(PlayerCamera*, bool);
    POINTER_SKYRIMSE(TLeaveBleedout, s_leaveBleedout, 50824);
    s_leaveBleedout(this, aInstant);
}
