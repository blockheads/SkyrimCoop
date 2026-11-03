#include <Actor.h>
#include <Combat/CombatController.h>
#include <Combat/CombatGroup.h>
#include <Combat/CombatTargetSelector.h>
#include <Combat/PartyAggro.h>
#include <Events/ActorDamagedEvent.h>
#include <Forms/ActorValueInfo.h>
#include <Forms/BGSEncounterZone.h>
#include <Games/ActorExtension.h>
#include <Havok/Raycast.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Services/Host/PartyThreat.h>
#include <VersionDb.h>
#include <World.h>
#include <atomic>
#include <cmath>
#include <memory>

namespace PartyAggroNative
{
namespace
{
std::atomic<std::shared_ptr<const PartyThreat::State>> s_state;
std::atomic<bool> s_active{false};

using THandleDamage = void(Actor*, Actor*, float);
THandleDamage* RealHandleDamage{};
// Actor/Character/PlayerCharacter vfunc 0x104 all use 38606 on both supported runtimes.
void HandleDamage(Actor* apVictim, Actor* apAttacker, float aDelta)
{
    RealHandleDamage(apVictim, apAttacker, aDelta);
    if (s_active.load(std::memory_order_acquire) && apVictim && apAttacker && !apVictim->GetExtension()->IsCopy() &&
        !apVictim->GetExtension()->IsPlayer() && std::isfinite(aDelta) && aDelta < 0)
        World::Get().GetRunner().Trigger(ActorDamagedEvent{apVictim->formID, apAttacker->formID, -aDelta});
}

using TSelect = uint32_t*(CombatTargetSelectorStandard*, uint32_t*);
TSelect* RealSelect{};
// 47191 returns its 32-bit handle through the caller's second-argument buffer (vfunc 6).
uint32_t* Select(CombatTargetSelectorStandard* apSelector, uint32_t* apResult)
{
    auto* pResult = RealSelect(apSelector, apResult);
    if (!pResult || !*pResult || !s_active.load(std::memory_order_acquire))
        return pResult;
    const auto cState = s_state.load(std::memory_order_acquire);
    if (!cState || !cState->Active)
        return pResult;
    auto* pController = apSelector->pCombatController;
    auto* pActor = pController ? pController->GetAttacker() : nullptr;
    if (!pActor || pActor->GetExtension()->IsCopy() || pActor->GetExtension()->IsPlayer() ||
        pActor->VirtIsDead(false) || pActor->GetOwnCurrentScene() || (pActor->actorState.flags1 >> 28) != 0 ||
        !cState->Combats.contains(pActor->formID))
        return pResult;
    const auto* pOriginal = TESObjectREFR::GetByHandle(*pResult);
    if (!pOriginal || !cState->IsPlayer(pOriginal->formID))
        return pResult;
    const auto* pCurrent = TESObjectREFR::GetByHandle(pController->targetHandle);
    if (!pCurrent || !cState->IsPlayer(pCurrent->formID))
        return pResult;
    auto* pGroup = pController->pCombatGroup;
    if (!pGroup)
        return pResult;
    std::vector<CombatTarget> targets;
    {
        BSReadLockGuard guard(pGroup->lock);
        targets.assign(pGroup->targets.begin(), pGroup->targets.end());
    }
    using TZone = BGSEncounterZone*(Actor*);
    POINTER_SKYRIMSE(TZone, s_zone, 20203);
    using TValid = bool(CombatTargetSelector*, Actor*, Actor*, BGSEncounterZone*);
    POINTER_SKYRIMSE(TValid, s_valid, 47196);
    using TLineOfSight = bool(Actor*, TESObjectREFR*, bool&);
    POINTER_SKYRIMSE(TLineOfSight, s_lineOfSight, 53829);
    auto* pZone = s_zone.Get()(pActor);
    std::vector<PartyThreat::Candidate> candidates;
    std::vector<std::pair<uint32_t, uint32_t>> handles;
    for (const auto& cTarget : targets)
    {
        auto* pTarget = Cast<Actor>(TESObjectREFR::GetByHandle(cTarget.targetHandle));
        if (!pTarget || !cState->IsPlayer(pTarget->formID) || pTarget->VirtIsDead(false) || !pTarget->GetNiNode() ||
            !s_valid.Get()(apSelector, pActor, pTarget, pZone))
            continue;
        PartyThreat::Perception perception{
            .Known = (cTarget.flags & CombatTarget::KNOWN) != 0,
            .Lost = (cTarget.flags & CombatTarget::LOST) != 0,
            .Detection = cTarget.detectLevel,
            .Stealth = cTarget.stealthPoints,
            .Invisible = pTarget->GetActorValue(ActorValueInfo::kInvisibility) > 0,
            .Sneaking = pTarget->actorState.IsSneaking(),
        };
        if (!cState->CanTarget(pActor->formID, pTarget->formID, pCurrent->formID, perception) &&
            !perception.Invisible && !perception.Sneaking &&
            (pTarget->formID == pCurrent->formID || cState->RecentlyHit(pActor->formID, pTarget->formID)))
        {
            bool unused = false;
            perception.LineOfSight = s_lineOfSight.Get()(pActor, pTarget, unused);
            if (!perception.LineOfSight)
                perception.ClearPath = GameRaycast::HasClearPath(pActor, pTarget);
        }
        if (!cState->CanTarget(pActor->formID, pTarget->formID, pCurrent->formID, perception))
            continue;
        const float cDistance =
            std::hypot(pTarget->position.x - pActor->position.x, pTarget->position.y - pActor->position.y,
                       pTarget->position.z - pActor->position.z);
        candidates.push_back({pTarget->formID, cDistance, true});
        handles.emplace_back(pTarget->formID, cTarget.targetHandle);
    }
    const uint32_t cChosen = cState->Resolve(pActor->formID, pCurrent->formID, pOriginal->formID, candidates);
    if (cChosen != pOriginal->formID)
        for (const auto& [cActor, cHandle] : handles)
            if (cActor == cChosen)
            {
                *pResult = cHandle;
                apSelector->hTarget.handle.iBits = cHandle;
                if (cChosen != pCurrent->formID)
                    spdlog::debug("Party aggro: NPC {:08X} switches from {:08X} to {:08X}", pActor->formID,
                                  pCurrent->formID, cChosen);
                break;
            }
    return pResult;
}
Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(38606, &RealHandleDamage, HandleDamage);
    HOOK_ID(47191, &RealSelect, Select);
});
} // namespace
void Publish(const PartyThreat::State& acState) noexcept
{
    if (!acState.Active)
    {
        s_active.store(false, std::memory_order_release);
        s_state.store({}, std::memory_order_release);
        return;
    }
    s_state.store(std::make_shared<const PartyThreat::State>(acState), std::memory_order_release);
    s_active.store(true, std::memory_order_release);
}
} // namespace PartyAggroNative
