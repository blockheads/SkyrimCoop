#pragma once

#include <Structs/AnimationVariables.h>
#include <movement/CopyStep.h>
#include <cstdint>

// Where an actor is and how a copy is moved. One part of IActorBridge (ports/IActorBridge.h).
struct IActorMovement
{
    virtual ~IActorMovement() = default;

    virtual void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept = 0;
    virtual void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept = 0;
    // Resident game-thread operation; no pointers into a reloadable DLL retained. Leave: only its graph's variables
    // (its ragdoll places it); Step: its engine steps it there this frame; Place: put there.
    virtual bool ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3],
        const float aRotation[3], float aDirection, const AnimationVariables& aVariables, CopyMove aMove) noexcept = 0;
    virtual void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;
    // Alive and in knock states 1-4.
    [[nodiscard]] virtual bool IsKnockedDown(uint32_t aFormId) const noexcept = 0;
    // It lies in its ragdoll, dead or knocked down: the engine keeps it on the ragdoll (Actor::LiesInRagdoll).
    [[nodiscard]] virtual bool IsLyingInRagdoll(uint32_t aFormId) const noexcept = 0;
    // Its character controller on the ground (hkpCharacterStateType 0): not jumping, falling, swimming or flying.
    [[nodiscard]] virtual bool IsOnGround(uint32_t aFormId) const noexcept = 0;
    // Its capsule collides (filter bit 0x8000 clear): the engine turns that off in furniture (40063), far off, mounted.
    [[nodiscard]] virtual bool CapsuleCollides(uint32_t aFormId) const noexcept = 0;
    // A copy's capsule made to collide or not, as its owner's (CapsuleCollides).
    virtual void SetCapsuleCollision(uint32_t aFormId, bool aCollides) noexcept = 0;
    // The layer its own bodies are on in the physics world, 0 when out of it: the AI puts them there as it sits (40063),
    // and the crosshair takes a seated actor only by them (CrosshairPickData::Pick).
    [[nodiscard]] virtual uint8_t BodiesLayer(uint32_t aFormId) const noexcept = 0;
    // Its graphs step with the physics world, so their ragdoll driver drives its bodies (63562); detached in furniture.
    [[nodiscard]] virtual bool GraphsStepPhysics(uint32_t aFormId) const noexcept = 0;
    // A copy's bodies and graphs put as its owner's are, in the engine's order (40063 detaches the graphs first, 39912
    // takes the bodies out first). A copy runs no AI, so its engine never does it.
    virtual void SetCopyPhysics(uint32_t aFormId, uint8_t aBodiesLayer, bool aGraphsStep) noexcept = 0;
    virtual void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept = 0;
    virtual uint32_t GetActorParentCellFormId(uint32_t aFormId) const noexcept = 0;
    virtual uint32_t GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept = 0;
    // Its parent cell is attached here (in this game's loaded grid; TESObjectCELL state 7).
    virtual bool IsActorCellAttached(uint32_t aFormId) const noexcept = 0;
    /// Run interpolation update for a remote actor.
    virtual void ActorUpdateInterpolation(uint32_t aFormId, uint64_t aTick) noexcept = 0;
};
