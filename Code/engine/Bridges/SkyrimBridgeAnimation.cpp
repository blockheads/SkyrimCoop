// SkyrimBridge: an actor's graph and actions (IActorAnimation).
#include "SkyrimBridgeInternal.h"

uint64_t SkyrimBridge::GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept
{
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            return pExt->GraphDescriptorHash;
    return 0;
}

void SkyrimBridge::SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept
{
    Breadcrumbs::Record("SetAnimationGraphDescriptorHash", aFormId);
    if (Actor* pActor = GetActor(aFormId))
        if (ActorExtension* pExt = pActor->GetExtension())
            pExt->GraphDescriptorHash = aHash;
}

bool SkyrimBridge::IsAnimationGraphReady(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    return pActor && EngineVirtual(pActor->animationGraphHolder).IsReady();
}

void SkyrimBridge::ActorLoadAnimationVariables(uint32_t aFormId, const AnimationVariables& acVars) noexcept
{
    Breadcrumbs::Record("ActorLoadAnimationVariables", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->LoadAnimationVariables(acVars);
}

void SkyrimBridge::ActorSaveAnimationVariables(uint32_t aFormId, AnimationVariables& aVars) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->SaveAnimationVariables(aVars);
}

std::string SkyrimBridge::SaveActorGraphState(uint32_t aFormId) const noexcept
{
    std::string state;
    if (Actor* pActor = GetActor(aFormId))
        pActor->SaveAnimationGraphState(state);
    return state;
}

bool SkyrimBridge::LoadActorGraphState(uint32_t aFormId, const std::string& acState) noexcept
{
    Breadcrumbs::Record("LoadActorGraphState", aFormId);
    Actor* pActor = GetActor(aFormId);
    return pActor && pActor->LoadAnimationGraphState(acState);
}

ActionEvent SkyrimBridge::GetActorLatestAnimation(uint32_t aFormId) const noexcept
{
    Actor* pActor = GetActor(aFormId);
    if (!pActor)
        return {};
    return pActor->GetExtension()->LatestAnimation.Read();
}

void SkyrimBridge::SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acEvent) noexcept
{
    Breadcrumbs::Record("SetActorLatestAnimation", aFormId);
    Actor* pActor = GetActor(aFormId);
    if (pActor)
        pActor->GetExtension()->LatestAnimation.Write(acEvent);
}
