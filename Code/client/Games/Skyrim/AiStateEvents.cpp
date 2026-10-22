#include <AiStateEvents.h>

#include <Events/AiStateChangeEvent.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>
#include <World.h>

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
uint32_t s_users = 0;
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
