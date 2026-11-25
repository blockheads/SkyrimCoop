#include <Games/CopyCombatGroups.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <Bridges/IActorBridge.h>
#include <Combat/CombatController.h>
#include <Combat/CombatGroup.h>
#include <Combat/CombatManager.h>
#include <CopyCombat.h>
#include <Diagnostics/Counters.h>
#include <Games/ActorExtension.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

#include <unordered_map>

// A copy holds a real controller and group filled from its owner's fight; nothing on this game decides for it
// (docs/re/systems/combat-targets.md, "A copy's combat").

CombatManager* CombatManager::Get() noexcept
{
    POINTER_SKYRIMSE(CombatManager*, s_manager, 405246);
    return *s_manager.Get();
}

CombatGroup* CombatManager::GetGroup(uint32_t aGroupId) const noexcept
{
    BSReadLockGuard guard(const_cast<BSReadWriteLock&>(lock)); // the combat job adds and frees groups
    for (uint16_t i = 0; i < usedCount; ++i)
        if (groups[i] && groups[i]->groupID == aGroupId)
            return groups[i];
    return nullptr;
}

namespace
{
thread_local bool s_holding = false;
// The owner's group key -> this game's group its copies hold (main thread: SetCopyCombat). A group id is the slot it
// took (46899) and is reused: OwnersGroup checks its members still hold the key.
std::unordered_map<uint64_t, uint32_t> s_groups;

using TStartCombatGroup = CombatController*(CombatManager*, Actor*, Actor* apTarget);
using TJoinCombatGroup = CombatController*(CombatManager*, Actor*, CombatGroup*);
TStartCombatGroup* RealStartCombatGroup = nullptr;
TJoinCombatGroup* RealJoinCombatGroup = nullptr;

CombatGroup* OwnersGroup(CombatManager& aManager, uint64_t aKey) noexcept
{
    const auto cIt = aKey ? s_groups.find(aKey) : s_groups.end();
    CombatGroup* pGroup = cIt != s_groups.end() ? aManager.GetGroup(cIt->second) : nullptr;
    if (!pGroup)
        return nullptr;
    BSReadLockGuard lock(pGroup->lock);
    for (const CombatMember& cMember : pGroup->members)
        if (auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(cMember.hMember.handle.iBits)))
            return pActor->GetExtension()->IsCopy() && pActor->GetExtension()->OwnerCombatGroup == aKey ? pGroup : nullptr;
    return nullptr;
}

struct HoldScope
{
    HoldScope() noexcept
    {
        s_holding = true;
    }
    ~HoldScope()
    {
        s_holding = false;
    }
};

Actor* ActorById(uint32_t aFormId) noexcept
{
    return aFormId ? Cast<Actor>(TESForm::GetById(aFormId)) : nullptr;
}

// 46873 / 46874 return the controller; StartCombat stores it (0x1406C9755), so Hold does too.
void Take(Actor& aCopy, CombatController* apController) noexcept
{
    if (apController && !aCopy.pCombatController)
        aCopy.pCombatController = apController;
}

std::vector<uint32_t> TargetsOf(CombatGroup& aGroup) noexcept
{
    std::vector<uint32_t> targets;
    BSReadLockGuard lock(aGroup.lock);
    for (const CombatTarget& cTarget : aGroup.targets)
        if (const TESObjectREFR* pTarget = TESObjectREFR::GetByHandle(cTarget.targetHandle))
            targets.push_back(pTarget->formID);
    return targets;
}

void MatchTargets(Actor& aCopy, CombatGroup& aGroup, const std::vector<uint32_t>& acOwnerTargets) noexcept
{
    const auto cChanges = CopyCombat::Diff(TargetsOf(aGroup), acOwnerTargets);
    if (!cChanges.Add.empty() || !cChanges.Remove.empty())
        spdlog::debug("Copy {:X} combat group {:X}: owner's targets {}, adds {}, removes {}", aCopy.formID,
                      aGroup.groupID, acOwnerTargets.size(), cChanges.Add.size(), cChanges.Remove.size());
    for (const uint32_t cTarget : cChanges.Add)
        if (Actor* pTarget = ActorById(cTarget))
            RealStartCombatGroup(CombatManager::Get(), &aCopy, pTarget); // with a controller: AddTarget (44704)
    if (cChanges.Remove.empty())
        return;
    using TRemoveTarget = void(CombatGroup*, TESObjectREFR*);
    POINTER_SKYRIMSE(TRemoveTarget, s_removeTarget, 44705);
    BSWriteLockGuard lock(aGroup.lock);
    for (const uint32_t cTarget : cChanges.Remove)
        if (auto* pTarget = Cast<TESObjectREFR>(TESForm::GetById(cTarget)))
            s_removeTarget(&aGroup, pTarget);
}
} // namespace

namespace CopyCombatGroups
{
bool Holding() noexcept
{
    return s_holding;
}

bool HeldByOwners(const CombatGroup& acGroup) noexcept
{
    BSReadLockGuard lock(const_cast<BSReadWriteLock&>(acGroup.lock)); // 44703 runs on the combat job, Hold on main
    if (acGroup.members.length == 0)
        return false;
    for (const CombatMember& cMember : acGroup.members)
    {
        auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(cMember.hMember.handle.iBits));
        if (!pActor || !pActor->GetExtension()->IsCopy())
            return false;
    }
    return true;
}

void Hold(Actor& aCopy, const CopyCombatFields& acFields) noexcept
{
    CombatManager* pManager = CombatManager::Get();
    if (!pManager)
        return;
    CombatGroup* pOwnerGroup = OwnersGroup(*pManager, acFields.GroupKey);
    Actor* pFirstTarget = ActorById(acFields.TargetId);
    for (size_t i = 0; !pFirstTarget && i < acFields.GroupTargetIds.size(); ++i)
        pFirstTarget = ActorById(acFields.GroupTargetIds[i]);

    const HoldScope cHolding;
    switch (CopyCombat::Next(acFields.State, aCopy.pCombatController != nullptr, pOwnerGroup != nullptr,
                             pFirstTarget != nullptr))
    {
    case CopyCombat::Step::kStop: {
        using TStopCombat = void(Actor*);
        POINTER_SKYRIMSE(TStopCombat, s_stopCombat, 38566);
        s_stopCombat(&aCopy);
        aCopy.GetExtension()->OwnerCombatGroup = 0;
        return;
    }
    case CopyCombat::Step::kStart:
        Take(aCopy, RealStartCombatGroup(pManager, &aCopy, pFirstTarget));
        break;
    case CopyCombat::Step::kJoin:
        Take(aCopy, RealJoinCombatGroup(pManager, &aCopy, pOwnerGroup));
        break;
    case CopyCombat::Step::kNone:
        break;
    }

    CombatController* pController = aCopy.pCombatController;
    if (!pController || !pController->pCombatGroup)
        return;
    CombatGroup& group = *pController->pCombatGroup;
    aCopy.GetExtension()->OwnerCombatGroup = acFields.GroupKey;
    if (acFields.GroupKey)
        s_groups[acFields.GroupKey] = group.groupID;
    MatchTargets(aCopy, group, acFields.GroupTargetIds);
    {
        BSWriteLockGuard lock(group.lock);
        group.searchState = CopyCombat::SearchState(acFields.State);
    }
    Actor* pTarget = ActorById(acFields.TargetId);
    if (pController->targetHandle != (pTarget ? pTarget->GetHandle().handle.iBits : 0))
        pController->SetTarget(pTarget); // 33235, writes Actor::currentCombatTarget too
}
} // namespace CopyCombatGroups

namespace
{
// CombatManager__StartCombatGroup (46873) / __JoinCombatGroup (46874): a group and controller for an actor. Not for a
// copy unless Hold puts its owner's fight on it (the allies-join path reaches 46874 without StartCombat).
CombatController* HookStartCombatGroup(CombatManager* apManager, Actor* apActor, Actor* apTarget)
{
    if (!CopyCombat::MayStart(apActor->GetExtension()->IsCopy(), s_holding))
    {
        COUNT("copy combat group refused", 1);
        return nullptr;
    }
    return RealStartCombatGroup(apManager, apActor, apTarget);
}

CombatController* HookJoinCombatGroup(CombatManager* apManager, Actor* apActor, CombatGroup* apGroup)
{
    if (!CopyCombat::MayStart(apActor->GetExtension()->IsCopy(), s_holding))
    {
        COUNT("copy combat group refused", 1);
        return nullptr;
    }
    return RealJoinCombatGroup(apManager, apActor, apGroup);
}

using TUpdateCombat = uintptr_t(Actor*, uintptr_t);
TUpdateCombat* RealUpdateCombat = nullptr;

// Actor::UpdateCombat (38565, CombatUpdateJob 46902): the target pick, the stop check and the behaviour tree; all
// deciders, none for a copy. Its tail, UpdateCombatSearchState (37273), is bookkeeping (+0x104, flags2 0x4000): kept.
uintptr_t HookUpdateCombat(Actor* apActor, uintptr_t aArg)
{
    if (!apActor->GetExtension()->IsCopy())
        return RealUpdateCombat(apActor, aArg);
    using TUpdateSearchState = uintptr_t(Actor*);
    POINTER_SKYRIMSE(TUpdateSearchState, s_updateSearchState, 37273);
    return s_updateSearchState(apActor);
}

Hooks::Initializer s_copyCombatGroupHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(46873, &RealStartCombatGroup, HookStartCombatGroup);
    HOOK_ID(46874, &RealJoinCombatGroup, HookJoinCombatGroup);
    HOOK_ID(38565, &RealUpdateCombat, HookUpdateCombat);
});
} // namespace
