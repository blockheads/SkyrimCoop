// SkyrimBridge: where an actor is and how its copy is moved (IActorMovement).
#include "SkyrimBridgeInternal.h"

void SkyrimBridge::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    Breadcrumbs::Record("MoveActorTo", aFormId, aCellId);
    Actor* pActor = GetActor(aFormId);
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(aCellId));
    if (pActor && pCell)
    {
        NiPoint3 pos;
        pos.x = aX;
        pos.y = aY;
        pos.z = aZ;
        // The goal is the place it leaves: the movement job would step it back there (37356). Before the move, which
        // can delete the actor.
        if (auto* pExtension = pActor->GetExtension(); pExtension && pExtension->IsCopy())
            pExtension->HasCopyGoal = false;
        pActor->MoveTo(pCell, pos);
    }
}

void SkyrimBridge::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    Breadcrumbs::Record("SetActorRotation", aFormId);
    if (auto* pActor = GetActor(aFormId))
        pActor->SetRotation(aX, 0.f, aZ);
}

bool SkyrimBridge::ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3],
    const float aRotation[3], float aDirection, const AnimationVariables& aVariables, CopyMove aMove) noexcept
{
    Breadcrumbs::Record("ApplyRemoteMovement", aFormId);
    auto* actor = GetActor(aFormId);
    if (!actor) return false;
    NiPoint3 position;
    position.x = aPosition[0];
    position.y = aPosition[1];
    position.z = aPosition[2];
    // A copy lying in its ragdoll is kept on it by the engine in its graph update, as its owner (HookTransformDelta).
    const CopyMove cMove = actor->LiesInRagdoll() ? CopyMove::Leave : aMove;
    const bool cPlace = cMove != CopyMove::Leave;
    // Stepped in its movement job (HookApplyMovement), which places it when its engine cannot step it.
    auto* pExtension = actor->GetExtension();
    pExtension->HasCopyGoal = cMove == CopyMove::Step;
    std::copy_n(aPosition, 3, pExtension->CopyGoal);
    if (cMove == CopyMove::Place)
        actor->ForcePosition(position);
    actor->LoadAnimationVariables(aVariables);
    if (actor->currentProcess && actor->currentProcess->middleProcess)
        actor->currentProcess->middleProcess->direction = aDirection;
    if (cPlace)
        actor->SetRotation(aRotation[0], aRotation[1], aRotation[2]);
    return true;
}

void SkyrimBridge::GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        aX = pActor->position.x;
        aY = pActor->position.y;
        aZ = pActor->position.z;
    }
}

bool SkyrimBridge::IsKnockedDown(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && !pActor->IsDead() && pActor->actorState.IsKnockedDown();
}

bool SkyrimBridge::IsLyingInRagdoll(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->LiesInRagdoll();
}

bool SkyrimBridge::IsOnGround(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    const auto* pProcess = pActor ? pActor->currentProcess : nullptr;
    const auto* pController = pProcess && pProcess->middleProcess ? pProcess->middleProcess->charController : nullptr;
    return pController && pController->state == bhkCharacterController::kOnGround;
}

bool SkyrimBridge::CapsuleCollides(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return !pActor || pActor->CapsuleCollides();
}

void SkyrimBridge::SetCapsuleCollision(uint32_t aFormId, bool aCollides) noexcept
{
    if (Actor* pActor = GetActor(aFormId); pActor && pActor->GetExtension()->IsCopy())
        pActor->SetCapsuleCollision(aCollides);
}

uint8_t SkyrimBridge::BodiesLayer(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor ? pActor->BodiesLayer() : 0;
}

bool SkyrimBridge::GraphsStepPhysics(uint32_t aFormId) const noexcept
{
    // Without graphs, as the engine leaves them: stepping.
    const Actor* pActor = GetActor(aFormId);
    return !pActor || ::GraphsStepPhysics(*pActor).value_or(true);
}

void SkyrimBridge::SetCopyPhysics(uint32_t aFormId, uint8_t aBodiesLayer, bool aGraphsStep) noexcept
{
    // Not while those bodies are its own ragdoll's: lying in it (BodyService) or in its kill move.
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->GetExtension()->IsCopy() || !ReplayState::TakesOwnerBodies(pActor->LiesInRagdoll(), pActor->IsInKillMove()))
        return;
    if (!aGraphsStep)
        SetGraphsStepPhysics(*pActor, false);
    // Linked to a mount, its bodies take the mount's collision group, as its owner's do (SeatRider 40358): in its own
    // group the horse climbed onto them and slid 70 units off (player-mount.md).
    pActor->SetBodiesLayer(aBodiesLayer, GetActor(GetActorMount(aFormId)));
    if (aGraphsStep)
        SetGraphsStepPhysics(*pActor, true);
}

void SkyrimBridge::GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
    {
        aX = pActor->rotation.x;
        aY = pActor->rotation.y;
        aZ = pActor->rotation.z;
    }
}

uint32_t SkyrimBridge::GetActorParentCellFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor || !pActor->parentCell)
        return 0;
    return pActor->parentCell->formID;
}

uint32_t SkyrimBridge::GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return 0;
    auto* pWS = pActor->GetWorldSpace();
    return pWS ? pWS->formID : 0;
}

bool SkyrimBridge::IsActorCellAttached(uint32_t aFormId) const noexcept
{
    const Actor* pActor = GetActor(aFormId);
    return pActor && pActor->parentCell && pActor->parentCell->IsValid();
}

void SkyrimBridge::ActorUpdateInterpolation(uint32_t aFormId, uint64_t aTick) noexcept
{
    Breadcrumbs::Record("ActorUpdateInterpolation", aFormId);
    // InterpolationSystem::Update requires InterpolationComponent; called directly in CharacterService
}
