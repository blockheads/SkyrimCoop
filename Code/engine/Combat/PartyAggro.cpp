#include <Actor.h>
#include <Combat/CombatController.h>
#include <Combat/CombatGroup.h>
#include <Combat/CombatTargetSelector.h>
#include <Combat/PartyAggro.h>
#include <characters/ActorDamagedEvent.h>
#include <Forms/ActorValueInfo.h>
#include <Forms/BGSEncounterZone.h>
#include <combat/ActorUpkeep.h>
#include <ActorExtension.h>
#include <Havok/Raycast.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <combat/PartyThreat.h>
#include <VersionDb.h>
#include <core/World.h>
#include <atomic>
#include <cmath>
#include <memory>

namespace
{
// Who is angry with which guest's copy: actor << 32 | copy. Read lock-free from the AI threads' reaction hooks
// (IsPartyEnemy); only the main thread writes.
constexpr size_t cAngerSlots = 32;
std::atomic<uint64_t> s_angerKeys[cAngerSlots]{};
bool s_angerFought[cAngerSlots]{}; // main thread only: has fought them since getting angry
std::atomic<size_t> s_angerCount{0};

uint64_t AngerKey(const Actor* apActor, const Actor* apOther) noexcept
{
    return (uint64_t(apActor->formID) << 32) | apOther->formID;
}
} // namespace

void MarkAngryWithRemotePlayer(Actor* apActor, Actor* apRemotePlayer) noexcept
{
    const uint64_t cKey = AngerKey(apActor, apRemotePlayer);
    size_t free = cAngerSlots;
    for (size_t i = 0; i < cAngerSlots; ++i)
    {
        const uint64_t cSlot = s_angerKeys[i].load(std::memory_order_relaxed);
        if (cSlot == cKey)
            return;
        if (cSlot == 0 && free == cAngerSlots)
            free = i;
    }
    if (free == cAngerSlots)
        return;
    s_angerFought[free] = false;
    s_angerKeys[free].store(cKey, std::memory_order_release);
    s_angerCount.fetch_add(1, std::memory_order_relaxed);
}

void UpdateRemotePlayerAnger() noexcept
{
    if (s_angerCount.load(std::memory_order_relaxed) == 0)
        return;

    for (size_t i = 0; i < cAngerSlots; ++i)
    {
        const uint64_t cKey = s_angerKeys[i].load(std::memory_order_relaxed);
        if (!cKey)
            continue;
        auto* pActor = Cast<Actor>(TESForm::GetById(static_cast<uint32_t>(cKey >> 32)));
        auto* pRemotePlayer = Cast<Actor>(TESForm::GetById(static_cast<uint32_t>(cKey)));
        bool resolved = !pActor || !pRemotePlayer || pActor->IsDead() || !pActor->GetNiNode();
        // Skyrim decides when the fight is over (yield, flee, death, distance...): once the actor
        // has fought them and is out of combat, the anger is resolved, like the engine's own
        // "angry with player" for its player.
        if (!resolved)
        {
            Actor* pTarget = pActor->GetCombatTarget();
            s_angerFought[i] = PartyThreat::AngerFought(s_angerFought[i], pTarget && pTarget->GetExtension()->IsPlayer());
            if (s_angerFought[i] && !pActor->IsInCombat())
                resolved = true;
        }
        if (resolved)
        {
            spdlog::info("Anger of {:X} at remote player {:X} resolved", static_cast<uint32_t>(cKey >> 32), static_cast<uint32_t>(cKey));
            s_angerKeys[i].store(0, std::memory_order_release);
            s_angerCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

namespace PartyAggroNative
{
namespace
{
// Angry with one player's copy: every player's body is its enemy (PartyThreat::AngerCovers).
bool IsAngryWithParty(Actor* apActor, Actor* apOther) noexcept
{
    if (!apActor || !apOther || s_angerCount.load(std::memory_order_relaxed) == 0)
        return false;
    const bool cOtherIsPlayer = apOther->GetExtension()->IsPlayer();
    for (const auto& cSlot : s_angerKeys)
        if (PartyThreat::AngerCovers(cSlot.load(std::memory_order_acquire), apActor->formID, cOtherIsPlayer))
            return true;
    return false;
}

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
        World::Runner().Trigger(ActorDamagedEvent{apVictim->formID, apAttacker->formID, -aDelta});
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
bool IsPartyEnemy(Actor* apActor, Actor* apTarget) noexcept
{
    if (IsAngryWithParty(apActor, apTarget))
        return true;
    if (!apActor || !apTarget || !s_active.load(std::memory_order_acquire) || !apTarget->GetExtension()->IsPlayer() ||
        apActor->GetExtension()->IsCopy() || apActor->GetExtension()->IsPlayer())
        return false;
    const auto cState = s_state.load(std::memory_order_acquire);
    return cState && cState->IsPartyFoe(apActor->formID, apTarget->formID);
}
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
