#include <ActorValueEvents.h>

#include <Actor.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/EventDispatcher.h>
#include <Games/ActorExtension.h>
#include <Games/DeathActions.h>
#include <Games/Events.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/ActorState.h>
#include <VersionDb.h>
#include <World.h>

#include <atomic>

static_assert((DeathActions::kDying >> 21) == ActorState::kLifeDying);
extern thread_local bool g_forceAnimation;

namespace ActorValueEvents
{
namespace
{
std::atomic<bool> s_active = false;
uint32_t s_users = 0;

// 38470: every nonzero base/modifier write calls the engine's update sinks after storing the value.
using TUpdate = void(Actor*, uint32_t, float, float, Actor*);
TUpdate* RealUpdate = nullptr;

void HookUpdate(Actor* apActor, uint32_t aValue, float aOldValue, float aDelta, Actor* apSource)
{
    RealUpdate(apActor, aValue, aOldValue, aDelta, apSource);
    if (s_active && apActor)
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{apActor->formID, aValue});
}

// Actor::Resurrect (37321): the 1.6.1170 body consumes only resetInventory (DL).
using TResurrect = void(Actor*, bool);
TResurrect* RealResurrect = nullptr;

void HookResurrect(Actor* apActor, bool aResetInventory)
{
    if (!s_active)
    {
        RealResurrect(apActor, aResetInventory);
        return;
    }
    const bool cWasDead = apActor->IsDead();
    const auto cAction = apActor->GetExtension()->LatestAnimation;
    std::optional<uint64_t> tick;
    if (cAction.ActionId && DeathActions::IsDying(cAction.State1))
        tick = cAction.Tick;
    DeathActions::WithLocalReset(g_forceAnimation, [&] { RealResurrect(apActor, aResetInventory); });
    if (s_active && cWasDead != apActor->IsDead())
        World::Get().GetRunner().Trigger(ActorDeathChangeEvent{apActor->formID, true, true, tick});
}

struct DeathSink final : BSTEventSink<TESDeathEvent>
{
    BSTEventResult OnEvent(const TESDeathEvent* apEvent, const EventDispatcher<TESDeathEvent>*) override
    {
        if (s_active && apEvent && apEvent->pActorDying)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{apEvent->pActorDying->formID, apEvent->isDead});
        return BSTEventResult::kOk;
    }
};
DeathSink s_death;

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(38470, &RealUpdate, HookUpdate);
    HOOK_ID(37321, &RealResurrect, HookResurrect);
});
} // namespace

void Start() noexcept
{
    if (s_users++ != 0)
        return;
    s_active = true;
    if (auto* pEvents = EventDispatcherManager::Get())
        pEvents->deathEvent.RegisterSink(&s_death);
}

void Stop() noexcept
{
    if (!s_users || --s_users != 0)
        return;
    s_active = false;
    if (auto* pEvents = EventDispatcherManager::Get())
        pEvents->deathEvent.UnRegisterSink(&s_death);
}
} // namespace ActorValueEvents
