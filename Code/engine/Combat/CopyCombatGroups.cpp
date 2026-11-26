#include <CopyCombatGroups.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <ports/IActorBridge.h>
#include <Combat/CombatController.h>
#include <Combat/CombatGroup.h>
#include <Combat/CombatManager.h>
#include <CopyCombat.h>
#include <core/Counters.h>
#include <ActorExtension.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

#include <unordered_map>
#include <vector>

// A copy holds a real controller and group filled from its owner's fight; nothing on this game decides for it
// (docs/re/systems/combat-targets.md, "A copy's combat").

CombatManager* CombatManager::Get() noexcept
{
    POINTER_SKYRIMSE(CombatManager*, s_manager, 405246);
    return *s_manager.Get();
}

namespace
{
thread_local bool s_holding = false;
// The owner's group key -> the copies holding it (main thread: SetCopyCombat). Their controllers lead to the group:
// the manager's array is not read (the combat job grows it without a lock, 46899).
std::unordered_map<uint64_t, std::vector<uint32_t>> s_holders;

using TStartCombatGroup = CombatController*(CombatManager*, Actor*, Actor* apTarget);
using TJoinCombatGroup = CombatController*(CombatManager*, Actor*, CombatGroup*);
TStartCombatGroup* RealStartCombatGroup = nullptr;
TJoinCombatGroup* RealJoinCombatGroup = nullptr;

// StartCombat's spinlock (403935) around the group primitives and storing the controller, taken as it does.
struct StartLock
{
    StartLock() noexcept
    {
        POINTER_SKYRIMSE(volatile long, s_lock, 403935);
        m_pLock = s_lock.Get();
        while (_InterlockedCompareExchange(m_pLock, 1, 0) != 0)
            Sleep(0);
        s_holding = true;
    }
    ~StartLock()
    {
        s_holding = false;
        _InterlockedExchange(m_pLock, 0);
    }

private:
    volatile long* m_pLock;
};

Actor* ActorById(uint32_t aFormId) noexcept
{
    return aFormId ? Cast<Actor>(TESForm::GetById(aFormId)) : nullptr;
}

CombatGroup* GroupOf(const Actor& acActor) noexcept
{
    return acActor.pCombatController ? acActor.pCombatController->pCombatGroup : nullptr;
}

// A copy still holding the key leads to its group.
CombatGroup* OwnersGroup(uint64_t aKey) noexcept
{
    const auto cIt = aKey ? s_holders.find(aKey) : s_holders.end();
    if (cIt == s_holders.end())
        return nullptr;
    auto& holders = cIt->second;
    std::erase_if(holders, [aKey](uint32_t aFormId) {
        Actor* pActor = ActorById(aFormId);
        return !pActor || !pActor->GetExtension()->IsCopy() || pActor->GetExtension()->OwnerCombatGroup != aKey ||
               !GroupOf(*pActor);
    });
    return holders.empty() ? nullptr : GroupOf(*ActorById(holders.front()));
}

void Stop(Actor& aCopy) noexcept
{
    using TStopCombat = void(Actor*);
    POINTER_SKYRIMSE(TStopCombat, s_stopCombat, 38566);
    s_stopCombat(&aCopy);
    aCopy.GetExtension()->OwnerCombatGroup = 0;
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
        // A member taken for a kill move stays its owner's fight's (KillHeld): the rest are not pruned meanwhile.
        auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(cMember.hMember.handle.iBits));
        const auto* pExtension = pActor ? pActor->GetExtension() : nullptr;
        if (!pExtension || !(pExtension->IsCopy() || pExtension->KillHeld.load(std::memory_order_acquire)))
            return false;
    }
    return true;
}

void Hold(Actor& aCopy, const CopyCombatFields& acFields) noexcept
{
    CombatManager* pManager = CombatManager::Get();
    if (!pManager)
        return;
    auto* pExtension = aCopy.GetExtension();
    Actor* pFirstTarget = ActorById(acFields.TargetId);
    for (size_t i = 0; !pFirstTarget && i < acFields.GroupTargetIds.size(); ++i)
        pFirstTarget = ActorById(acFields.GroupTargetIds[i]);

    {
        const StartLock cLock;
        CombatGroup* pOwnerGroup = OwnersGroup(acFields.GroupKey);
        auto step = CopyCombat::Next(acFields.GroupKey, pExtension->OwnerCombatGroup, aCopy.pCombatController != nullptr,
                                     pOwnerGroup != nullptr, pFirstTarget != nullptr);
        if (step == CopyCombat::Step::kStop)
        {
            // Its owner left the group, or a controller from another key (a fight it had while this game's).
            Stop(aCopy);
            step = CopyCombat::Next(acFields.GroupKey, 0, false, pOwnerGroup != nullptr, pFirstTarget != nullptr);
        }
        // 46873 / 46874 return the controller; StartCombat stores it (0x1406C9755).
        CombatController* pNew = nullptr;
        if (step == CopyCombat::Step::kStart)
            pNew = RealStartCombatGroup(pManager, &aCopy, pFirstTarget);
        else if (step == CopyCombat::Step::kJoin)
            pNew = RealJoinCombatGroup(pManager, &aCopy, pOwnerGroup);
        if (pNew && !aCopy.pCombatController)
            aCopy.pCombatController = pNew;

        CombatGroup* pGroup = GroupOf(aCopy);
        if (!pGroup)
            return;
        pExtension->OwnerCombatGroup = acFields.GroupKey;
        auto& holders = s_holders[acFields.GroupKey];
        if (std::ranges::find(holders, aCopy.formID) == holders.end())
            holders.push_back(aCopy.formID);
        MatchTargets(aCopy, *pGroup, acFields.GroupTargetIds);
        BSWriteLockGuard lock(pGroup->lock);
        pGroup->searchState = CopyCombat::SearchState(acFields.State);
    }
    CombatController* pController = aCopy.pCombatController;
    // A member out of combat stays in its group, as its owner does (41240 lists it): IsInCombat reads +0x43.
    pController->inactive = CopyCombat::Inactive(acFields.State);
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

using TUpdateCombat = void(Actor*);
TUpdateCombat* RealUpdateCombat = nullptr;

// Actor::UpdateCombat (38565, CommonLib void UpdateCombat()): the deciders are not run for a copy; its guard and its
// tail, UpdateCombatSearchState (37273: +0x104, kSearchingInCombat), are.
void HookUpdateCombat(Actor* apActor)
{
    if (!apActor->GetExtension()->IsCopy())
        return RealUpdateCombat(apActor);
    if (CopyCombat::SkipsUpdate(apActor->flags))
        return;
    using TUpdateSearchState = void(Actor*);
    POINTER_SKYRIMSE(TUpdateSearchState, s_updateSearchState, 37273);
    s_updateSearchState(apActor);
}

Hooks::Initializer s_copyCombatGroupHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(46873, &RealStartCombatGroup, HookStartCombatGroup);
    HOOK_ID(46874, &RealJoinCombatGroup, HookJoinCombatGroup);
    HOOK_ID(38565, &RealUpdateCombat, HookUpdateCombat);
});
} // namespace
