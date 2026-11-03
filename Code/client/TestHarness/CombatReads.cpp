#include "EngineCalls.h"
#include <Actor.h>
#include <Combat/CombatController.h>
#include <Combat/CombatGroup.h>
#include <Forms/ActorValueInfo.h>
#include <Games/Primitives.h>
#include <VersionDb.h>
#include <Havok/Raycast.h>

namespace TestHarness::EngineCalls
{
std::optional<CombatRead> ReadCombat(uint32_t aActorId)
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aActorId));
    if (!pActor)
        return {};
    CombatRead read;
    read.State = pActor->GetOwnCombatState();
    const auto form = [](uint32_t aHandle) {
        const auto* pRef = TESObjectREFR::GetByHandle(aHandle);
        return pRef ? pRef->formID : 0;
    };
    read.ActorTarget = form(pActor->combatHandle);
    const auto* pTarget = pActor->GetCombatTarget();
    read.PapyrusTarget = pTarget ? pTarget->formID : 0;
    auto* pController = pActor->pCombatController;
    if (!pController)
        return read;
    read.ControllerTarget = form(pController->targetHandle);
    auto* pGroup = pController->pCombatGroup;
    if (!pGroup)
        return read;
    std::vector<CombatCandidateRead> handles;
    {
        BSReadLockGuard guard(pGroup->lock);
        for (const auto& cTarget : pGroup->targets)
            handles.push_back({cTarget.targetHandle, cTarget.detectLevel, cTarget.attackerCount,
                               uint16_t(cTarget.flags), cTarget.stealthPoints});
    }
    for (auto candidate : handles)
    {
        candidate.FormId = form(candidate.FormId);
        if (auto* pTarget = Cast<Actor>(TESForm::GetById(candidate.FormId)))
        {
            using TLineOfSight = bool(Actor*, TESObjectREFR*, bool&);
            POINTER_SKYRIMSE(TLineOfSight, s_lineOfSight, 53829);
            bool unused = false;
            candidate.LineOfSight = s_lineOfSight.Get()(pActor, pTarget, unused);
            candidate.ClearPath = GameRaycast::HasClearPath(pActor, pTarget);
            candidate.Invisible = pTarget->GetActorValue(ActorValueInfo::kInvisibility) > 0;
            candidate.Sneaking = pTarget->actorState.IsSneaking();
        }
        read.Candidates.push_back(candidate);
    }
    return read;
}
} // namespace TestHarness::EngineCalls
