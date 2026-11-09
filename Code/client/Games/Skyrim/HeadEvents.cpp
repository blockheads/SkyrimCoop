#include <Actor.h>
#include <Events/ActorHeadUpdateEvent.h>
#include <Forms/TESNPC.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <World.h>

namespace
{
using TUpdateHeadState = uint64_t(TESNPC*, Actor*, NiAVObject*);
TUpdateHeadState* RealUpdateHeadState = nullptr;

// TESNPC::UpdateHeadState (24724; skee64's hook): the tail of every head attach (TESNPC 24732, 24740) and of worn
// changes (24725). Queued: queued head loads can finish off the main thread.
uint64_t HookUpdateHeadState(TESNPC* apNpc, Actor* apActor, NiAVObject* apRoot)
{
    const uint64_t cResult = RealUpdateHeadState(apNpc, apActor, apRoot);
    if (apActor)
        World::Get().GetRunner().Trigger(ActorHeadUpdateEvent{apActor->formID});
    return cResult;
}

Hooks::Initializer s_headHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(24724, &RealUpdateHeadState, HookUpdateHeadState);
    });
} // namespace
