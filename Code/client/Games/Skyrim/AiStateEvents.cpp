#include <AiStateEvents.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <AI/AIProcess.h>
#include <Games/ActorExtension.h>
#include <Combat/CombatController.h>
#include <Combat/CombatGroup.h>
#include <Games/CopyCombatGroups.h>
#include <DetectionRules.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <VersionDb.h>
#include <World.h>
#include <WorldGate.h>

#include <atomic>

namespace AiStateEvents
{
namespace
{
// Each runs where the engine sends it (the AI's job threads too): the change is queued.
void Raise(const TESObjectREFR* apActor) noexcept
{
    if (apActor)
        World::Get().GetRunner().Trigger(AiStateChangeEvent{apActor->formID});
}

struct PackageSink final : BSTEventSink<TESPackageEvent>
{
    BSTEventResult OnEvent(const TESPackageEvent* apEvent, const EventDispatcher<TESPackageEvent>*) override
    {
        if (apEvent)
            Raise(apEvent->actor);
        return BSTEventResult::kOk;
    }
};

struct CombatSink final : BSTEventSink<TESCombatEvent>
{
    BSTEventResult OnEvent(const TESCombatEvent* apEvent, const EventDispatcher<TESCombatEvent>*) override
    {
        if (apEvent)
            Raise(apEvent->actor);
        return BSTEventResult::kOk;
    }
};

// A death ends the actor's package and combat without their own events.
struct DeathSink final : BSTEventSink<TESDeathEvent>
{
    BSTEventResult OnEvent(const TESDeathEvent* apEvent, const EventDispatcher<TESDeathEvent>*) override
    {
        if (apEvent)
            Raise(apEvent->pActorDying);
        return BSTEventResult::kOk;
    }
};

// A scene began or ended: its actors are not in the event.
struct SceneSink final : BSTEventSink<TESSceneEvent>
{
    BSTEventResult OnEvent(const TESSceneEvent*, const EventDispatcher<TESSceneEvent>*) override
    {
        World::Get().GetRunner().Trigger(AiStateChangeEvent{});
        return BSTEventResult::kOk;
    }
};

// It sat in or stood from a furniture (AIProcess::SetSitSleepState 39912 sends both).
struct FurnitureSink final : BSTEventSink<TESFurnitureEvent>
{
    BSTEventResult OnEvent(const TESFurnitureEvent* apEvent, const EventDispatcher<TESFurnitureEvent>*) override
    {
        if (apEvent)
            Raise(apEvent->actor);
        return BSTEventResult::kOk;
    }
};

PackageSink s_package;
FurnitureSink s_furniture;
CombatSink s_combat;
DeathSink s_death;
SceneSink s_scene;
std::atomic<uint32_t> s_users{0};

using TSetCombatTarget = void(CombatController*, Actor*);
TSetCombatTarget* RealSetCombatTarget = nullptr;

// CombatController::SetTarget (33235), from Actor::UpdateCombat: writes Actor::currentCombatTarget (+0x104). A
// switch inside a fight sends no TESCombatEvent (that is a combat state change).
void HookSetCombatTarget(CombatController* apController, Actor* apTarget)
{
    RealSetCombatTarget(apController, apTarget);
    if (s_users.load(std::memory_order_relaxed))
        Raise(apController->GetAttacker());
}

using TUpdatePlayerInCombat = void(PlayerCharacter*, float);
TUpdatePlayerInCombat* RealUpdatePlayerInCombat = nullptr;

// PlayerCharacter::UpdateInCombat (41238) recounts the player's in-combat flag each update; no event says it changed.
void HookUpdatePlayerInCombat(PlayerCharacter* apPlayer, float aDelta)
{
    const bool cWas = apPlayer->IsFlaggedInCombat();
    RealUpdatePlayerInCombat(apPlayer, aDelta);
    if (cWas != apPlayer->IsFlaggedInCombat() && s_users.load(std::memory_order_relaxed))
        Raise(apPlayer);
}

using TUpdateCombatGroup = void(CombatGroup*);
TUpdateCombatGroup* RealUpdateCombatGroup = nullptr;

// CombatGroup's update (44703, on the combat manager's job) adds and drops its targets with no event: a change marks
// each member changed, as its state carries its group (combat-targets.md, "A copy's combat"). A group of copies only
// holds its owners' fight: its update, which would drop targets on this game's detection, is not run.
void HookUpdateCombatGroup(CombatGroup* apGroup)
{
    const WorldGate::Reader cWorld;
    if (cWorld && CopyCombatGroups::HeldByOwners(*apGroup))
        return;
    RealUpdateCombatGroup(apGroup);
    if (!cWorld || !s_users.load(std::memory_order_relaxed))
        return;
    BSReadLockGuard lock(apGroup->lock);
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const CombatTarget& cTarget : apGroup->targets)
        hash = (hash ^ cTarget.targetHandle) * 0x100000001B3ull;
    for (const CombatMember& cMember : apGroup->members)
    {
        auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(cMember.hMember.handle.iBits));
        if (pActor && pActor->GetExtension()->GroupTargetsSent.exchange(hash, std::memory_order_relaxed) != hash)
            Raise(pActor);
    }
}

// A copy's ProcessTracking is running on this thread.
thread_local bool s_copyTracking = false;

using TDetectionStateOf = void*(Actor*, Actor*, uint32_t);
TDetectionStateOf* RealDetectionStateOf = nullptr;

// Actor__GetFreshDetectionStateOf (37758): the actor's fresh detection state of a target, null with none.
// ProcessTracking asks it of this game's player and drops the look when it is not detected.
void* HookDetectionStateOf(Actor* apActor, Actor* apTarget, uint32_t aPriority)
{
    if (!DetectionRules::HeadTrackAsksDetection(s_copyTracking))
        return nullptr;
    return RealDetectionStateOf(apActor, apTarget, aPriority);
}

using TProcessTracking = void(Actor*, float, NiAVObject*);
TProcessTracking* RealProcessTracking = nullptr;

// Actor::ProcessTracking (38009, vfunc 0x122, from Actor::UpdateAnimation, copies too): turns the head to the
// highest set headtrack slot. The AI, packages and scripts set the slots (HighProcessData::SetHeadtrackTarget 39783)
// without an event: an owner's change is noticed here. A copy runs no AI: its owner's slots are put in first.
void HookProcessTracking(Actor* apActor, float aDelta, NiAVObject* apObject)
{
    HighProcessData* pHigh = apActor->currentProcess ? apActor->currentProcess->highProcess : nullptr;
    if (!pHigh || !s_users.load(std::memory_order_relaxed))
        return RealProcessTracking(apActor, aDelta, apObject);
    auto& seen = apActor->GetExtension()->HeadTrack;
    std::array<uint32_t, HighProcessData::kHeadTrackTypes> targets{};
    const bool cCopy = apActor->GetExtension()->IsCopy();
    if (cCopy)
    {
        std::ranges::transform(seen, targets.begin(), [](const auto& acSlot) { return acSlot.load(std::memory_order_relaxed); });
        pHigh->SetHeadTrackTargets(targets);
    }
    s_copyTracking = cCopy;
    RealProcessTracking(apActor, aDelta, apObject);
    s_copyTracking = false;
    if (cCopy)
        return;
    targets = pHigh->GetHeadTrackTargets();
    bool changed = false;
    for (size_t i = 0; i < targets.size(); ++i)
        changed |= seen[i].exchange(targets[i], std::memory_order_relaxed) != targets[i];
    if (changed)
        Raise(apActor);
}

Hooks::Initializer s_aiStateHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(33235, &RealSetCombatTarget, HookSetCombatTarget);
        HOOK_ID(44703, &RealUpdateCombatGroup, HookUpdateCombatGroup);
        HOOK_ID(37758, &RealDetectionStateOf, HookDetectionStateOf);
        HOOK_ID(38009, &RealProcessTracking, HookProcessTracking);
        HOOK_ID(41238, &RealUpdatePlayerInCombat, HookUpdatePlayerInCombat);
    });
} // namespace

void Start() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || s_users++ != 0)
        return;
    pEvents->packageEvent.RegisterSink(&s_package);
    pEvents->combatEvent.RegisterSink(&s_combat);
    pEvents->deathEvent.RegisterSink(&s_death);
    pEvents->sceneEvent.RegisterSink(&s_scene);
    pEvents->furnitureEvent.RegisterSink(&s_furniture);
}

void Stop() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || !s_users || --s_users != 0)
        return;
    pEvents->packageEvent.UnRegisterSink(&s_package);
    pEvents->combatEvent.UnRegisterSink(&s_combat);
    pEvents->deathEvent.UnRegisterSink(&s_death);
    pEvents->sceneEvent.UnRegisterSink(&s_scene);
    pEvents->furnitureEvent.UnRegisterSink(&s_furniture);
}
} // namespace AiStateEvents
