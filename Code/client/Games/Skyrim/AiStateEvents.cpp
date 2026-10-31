#include <AiStateEvents.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <AI/AIProcess.h>
#include <Games/ActorExtension.h>
#include <Combat/CombatController.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>
#include <VersionDb.h>
#include <World.h>

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

PackageSink s_package;
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
    RealProcessTracking(apActor, aDelta, apObject);
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
}
} // namespace AiStateEvents
